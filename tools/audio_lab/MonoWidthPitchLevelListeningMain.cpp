// Offline listening fixtures for the grouped pitch-level decision. Every file
// is exact additive synthesis from the shipped Width/Morph Fourier coefficients
// (monoWidthShippedCoefficients: per-anchor depths, aligned, two-tooth saw):
// no tables, no Width/phase lookup error and no oversampling, so the only
// difference between variants is how partials fade near the host guard.
//   oracle  - each partial tapered independently (one harmonic per level)
//   4bpo    - candidate grouped bank, 4 bands/octave
//   2bpo    - positive control, 2 bands/octave (expected to be audible)
//   diff    - unmatched 4bpo minus oracle, i.e. exactly what the candidate removes
// Candidate files are RMS-matched to the oracle over the whole file.
// A second set (ladder/) feeds identical, unmatched oscillator signals through
// the production NonlinearTptLadder at 1x and 4x with Mono's decimators; its
// diff files are the unmatched post-ladder difference. Both sets report how
// much of the difference lies below 15 kHz, where the grouping adds nothing
// before the ladder.
#include "MonoWidthPitchLevels.h"
#include "NonlinearTptLadder.h"

#include <vekt/dsp/OversamplingBank.h>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <array>
#include <cmath>
#include <complex>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <vector>

namespace
{
constexpr int topHarmonic = 1023; // capacity of a 2048-sample table
constexpr float playbackGain = 0.5f;
constexpr double fadeSeconds = 0.02;
constexpr double audibleLimitHz = 15'000.0;
constexpr int ladderBlockSize = 256;

struct Fixture
{
	const char* name;
	double morph, width, startHz, endHz, seconds;
	bool worstStatic; // choose the 4bpo model-worst static pitch in [startHz, endHz]
};

// Exponential glide or static note, phase-continuous; writes all variants at once.
std::array<std::vector<float>, 3> synthesize(const std::vector<std::complex<double>>& coefficients,
	const std::array<const std::vector<int>*, 3>& schedules, double rate, double startHz, double endHz, double seconds,
	int factor = 1)
{
	// Guard gains follow the host rate; samples are produced at the internal rate.
	const auto internalRate = rate * factor;
	const auto count = static_cast<int>(seconds * internalRate);
	std::array<std::vector<float>, 3> outputs;
	for (auto& output : outputs) output.resize(static_cast<std::size_t>(count));
	double phase {};
	for (int sample = 0; sample < count; ++sample)
	{
		const auto t = static_cast<double>(sample) / std::max(1, count - 1);
		const auto pitch = startHz * std::pow(endHz / startHz, t);
		const auto unit = std::polar(1.0, 2.0 * std::numbers::pi * phase);
		std::array<double, 3> values {};
		values.fill(coefficients.front().real());
		auto oscillator = unit;
		for (int harmonic = 1; harmonic < static_cast<int>(coefficients.size()); ++harmonic, oscillator *= unit)
		{
			const auto oracle = vekt::audio_lab::monoWidthGuardGain(harmonic, pitch, rate);
			if (oracle <= 0.0) break;
			const auto partial = 2.0 * (coefficients[static_cast<std::size_t>(harmonic)] * oscillator).real();
			for (std::size_t variant = 0; variant < 3; ++variant)
				values[variant] += partial * (schedules[variant] == nullptr ? oracle
					: vekt::audio_lab::monoWidthGroupedGain(harmonic, *schedules[variant], pitch, rate));
		}
		const auto fade = std::min({ 1.0, sample / (fadeSeconds * internalRate),
			(count - 1 - sample) / (fadeSeconds * internalRate) });
		for (std::size_t variant = 0; variant < 3; ++variant)
			outputs[variant][static_cast<std::size_t>(sample)] = static_cast<float>(values[variant] * fade);
		phase += pitch / internalRate;
		phase -= std::floor(phase);
	}
	return outputs;
}

// Oscillator samples at rate * factor through the production ladder, then Mono's decimator.
std::vector<float> throughLadder(const std::vector<float>& internal, double rate, int factor, float driveDb, float resonance)
{
	using namespace vekt::dsp;
	const OversamplingQuality quality = factor == 4 ? OversamplingQuality { OversamplingFactor::x4, OversamplingFilter::polyphaseFIR }
		: OversamplingQuality { OversamplingFactor::off, OversamplingFilter::polyphaseIIR };
	OversamplingBank<float> bank(1);
	bank.prepare(ladderBlockSize);
	bank.activate(quality);
	vekt::mono::NonlinearTptLadder ladder;
	ladder.prepare(rate * factor);
	const vekt::mono::NonlinearTptLadderSettings settings { std::min(12'000.0f, static_cast<float>(rate * factor) * 0.45f),
		resonance, driveDb, false, 0.0f };
	const auto hostCount = static_cast<int>(internal.size()) / factor;
	std::vector<float> output;
	output.reserve(static_cast<std::size_t>(hostCount));
	juce::AudioBuffer<float> buffer(1, ladderBlockSize);
	for (int start = 0; start < hostCount; start += ladderBlockSize)
	{
		const auto length = std::min(ladderBlockSize, hostCount - start);
		buffer.clear();
		juce::dsp::AudioBlock<float> host = juce::dsp::AudioBlock<float>(buffer).getSubBlock(0, static_cast<std::size_t>(length));
		const juce::dsp::AudioBlock<const float> input(host);
		auto upsampled = bank.processSamplesUp(input);
		for (int index = 0; index < length * factor; ++index)
			upsampled.setSample(0, index, ladder.processCoupled(internal[static_cast<std::size_t>(start * factor + index)], settings));
		bank.processSamplesDown(host);
		for (int index = 0; index < length; ++index) output.push_back(buffer.getSample(0, index));
	}
	return output;
}

// Welch-averaged power below a frequency (Hann, 16384-point segments, 50% overlap).
double powerBelow(const std::vector<float>& samples, double rate, double limitHz)
{
	constexpr int order = 14, size = 1 << order;
	juce::dsp::FFT fft(order);
	std::vector<float> frame(2 * size);
	double total {}, window {};
	int segments {};
	for (int index = 0; index < size; ++index)
	{
		const auto w = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * index / size);
		window += w * w;
	}
	for (std::size_t start = 0; start + size <= samples.size(); start += size / 2, ++segments)
	{
		std::fill(frame.begin(), frame.end(), 0.0f);
		for (int index = 0; index < size; ++index)
			frame[static_cast<std::size_t>(index)] = samples[start + static_cast<std::size_t>(index)]
				* static_cast<float>(0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * index / size));
		fft.performFrequencyOnlyForwardTransform(frame.data());
		const auto lastBin = static_cast<int>(limitHz / rate * size);
		for (int bin = 0; bin <= lastBin; ++bin)
			total += (bin == 0 ? 1.0 : 2.0) * static_cast<double>(frame[static_cast<std::size_t>(bin)]) * frame[static_cast<std::size_t>(bin)];
	}
	return segments == 0 ? 0.0 : total / (segments * window * size);
}

// Static notes only: largest level change, in dB, of any partial below limitHz that
// is within 60 dB of the strongest one. Phase-insensitive; one long Hann window
// gives both signals identical scalloping.
double worstPartialLevelChangeDb(const std::vector<float>& reference, const std::vector<float>& candidate,
	double rate, double pitch, double limitHz)
{
	constexpr int order = 17, size = 1 << order;
	const auto spectrum = [&](const std::vector<float>& samples)
	{
		std::vector<float> frame(2 * size);
		const auto start = (samples.size() - size) / 2;
		for (int index = 0; index < size; ++index)
			frame[static_cast<std::size_t>(index)] = samples[start + static_cast<std::size_t>(index)]
				* static_cast<float>(0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * index / size));
		juce::dsp::FFT(order).performFrequencyOnlyForwardTransform(frame.data());
		return frame;
	};
	const auto a = spectrum(reference), b = spectrum(candidate);
	std::vector<std::pair<double, double>> partials;
	double strongest {};
	for (auto harmonic = 1; harmonic * pitch < limitHz; ++harmonic)
	{
		const auto centre = static_cast<int>(std::lround(harmonic * pitch / rate * size));
		double peakA {}, peakB {};
		for (auto bin = centre - 2; bin <= centre + 2; ++bin)
		{
			peakA = std::max(peakA, static_cast<double>(a[static_cast<std::size_t>(bin)]));
			peakB = std::max(peakB, static_cast<double>(b[static_cast<std::size_t>(bin)]));
		}
		partials.emplace_back(peakA, peakB);
		strongest = std::max(strongest, peakA);
	}
	double worst {};
	for (const auto& [peakA, peakB] : partials)
		if (peakA > strongest * 1.0e-3) worst = std::max(worst, std::abs(20.0 * std::log10(peakB / peakA)));
	return worst;
}

double meanPower(const std::vector<float>& samples, std::size_t margin)
{
	double sum {};
	for (auto index = margin; index + margin < samples.size(); ++index)
		sum += static_cast<double>(samples[index]) * samples[index];
	return sum / static_cast<double>(samples.size() - 2 * margin);
}

double db(double power, double reference)
{
	return 10.0 * std::log10(std::max(power, 1.0e-30) / std::max(reference, 1.0e-30));
}

double rms(const std::vector<float>& samples)
{
	double sum {};
	for (const auto value : samples) sum += static_cast<double>(value) * value;
	return std::sqrt(sum / static_cast<double>(samples.size()));
}

bool writeWav(const juce::File& file, const std::vector<float>& samples, double rate, float gain)
{
	juce::AudioBuffer<float> buffer(1, static_cast<int>(samples.size()));
	for (std::size_t index = 0; index < samples.size(); ++index)
		buffer.setSample(0, static_cast<int>(index), samples[index] * gain);
	file.deleteFile();
	std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
	juce::WavAudioFormat format;
	auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions {}
		.withSampleRate(rate).withNumChannels(1).withBitsPerSample(32));
	return writer != nullptr && writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
}
}

int main(int argc, char** argv)
{
	if (argc != 2)
	{
		std::cerr << "Usage: VektMonoWidthPitchLevelListening output-directory\n";
		return 64;
	}
	juce::ScopedJuceInitialiser_GUI juceInitialiser;
	const juce::File directory { juce::String(argv[1]) };
	if (!directory.createDirectory()) return 1;
	std::ofstream csv(directory.getChildFile("fixtures.csv").getFullPathName().toStdString());
	if (!csv) return 1;
	csv << std::setprecision(8)
		<< "file,rate,morph,width,start_hz,end_hz,model_4bpo_worst_dbc,model_2bpo_worst_dbc,measured_4bpo_file_dbc,diff_below_15k_dbc,darkened_4bpo_hz,match_gain_4bpo,match_gain_2bpo\n";
	const auto fourBands = vekt::audio_lab::monoWidthLevelLimits(topHarmonic, 4.0);
	const auto twoBands = vekt::audio_lab::monoWidthLevelLimits(topHarmonic, 2.0);
	const std::array<Fixture, 10> fixtures { {
		{ "glide-high-square", 3.0, 50.0, 1500.0, 6000.0, 12.0, false },
		{ "glide-mid-saw", 2.0, 50.0, 220.0, 1760.0, 12.0, false },
		{ "glide-bass-sawsquare-w90", 2.5, 90.0, 30.0, 240.0, 12.0, false },
		{ "glide-high-square-w5", 3.0, 5.0, 1000.0, 4000.0, 12.0, false },
		{ "static-high-square", 3.0, 50.0, 1000.0, 8000.0, 4.0, true },
		{ "static-high-saw", 2.0, 50.0, 1000.0, 8000.0, 4.0, true },
		{ "static-high-square-w5", 3.0, 5.0, 1000.0, 8000.0, 4.0, true },
		{ "static-bass-saw", 2.0, 50.0, 30.0, 120.0, 4.0, true },
		{ "static-bass-sawsquare-w90", 2.5, 90.0, 30.0, 120.0, 4.0, true },
		{ "glide-high-saw-w95", 2.0, 95.0, 1500.0, 6000.0, 12.0, false },
	} };
	for (const auto rate : { 44'100.0, 48'000.0 })
		for (const auto& fixture : fixtures)
		{
			auto coefficients = vekt::audio_lab::monoWidthShippedCoefficients(fixture.morph, fixture.width);
			coefficients.resize(topHarmonic + 1);
			const auto modelDb = [&](const std::vector<int>& limits, double pitch)
			{
				const auto error = vekt::audio_lab::monoWidthLevelError(coefficients, limits, pitch, rate);
				return 10.0 * std::log10(std::max(error.errorPower, 1.0e-30) / error.signalPower);
			};
			auto startHz = fixture.startHz, endHz = fixture.endHz;
			if (fixture.worstStatic)
			{
				auto worst = -400.0, worstPitch = startHz;
				for (auto pitch = fixture.startHz; pitch <= fixture.endHz; pitch *= std::exp2(1.0 / 192.0))
					if (const auto value = modelDb(fourBands, pitch); value > worst) { worst = value; worstPitch = pitch; }
				startHz = endHz = worstPitch;
			}
			// Worst over the fixture's pitch path.
			double model4 = -400.0, model2 = -400.0, darkened = rate / 2.0;
			for (auto pitch = std::min(startHz, endHz); pitch <= std::max(startHz, endHz) * 1.000001;
				pitch *= std::exp2(1.0 / 192.0))
			{
				model4 = std::max(model4, modelDb(fourBands, pitch));
				model2 = std::max(model2, modelDb(twoBands, pitch));
				const auto error = vekt::audio_lab::monoWidthLevelError(coefficients, fourBands, pitch, rate);
				if (error.lowestDarkenedRatio > 0.0) darkened = std::min(darkened, error.lowestDarkenedRatio * rate / 2.0);
				if (fixture.worstStatic) break;
			}
			const auto outputs = synthesize(coefficients, { nullptr, &fourBands, &twoBands }, rate, startHz, endHz, fixture.seconds);
			const auto oracleRms = rms(outputs[0]);
			const auto gain4 = oracleRms / rms(outputs[1]);
			const auto gain2 = oracleRms / rms(outputs[2]);
			std::vector<float> difference(outputs[0].size());
			for (std::size_t index = 0; index < difference.size(); ++index)
				difference[index] = outputs[1][index] - outputs[0][index];
			// Unmatched whole-file difference, excluding fades; static notes should equal the model.
			double differencePower {}, oraclePower {};
			const auto margin = static_cast<std::size_t>(fadeSeconds * rate);
			for (auto index = margin; index + margin < difference.size(); ++index)
			{
				differencePower += static_cast<double>(difference[index]) * difference[index];
				oraclePower += static_cast<double>(outputs[0][index]) * outputs[0][index];
			}
			const auto measured = 10.0 * std::log10(std::max(differencePower, 1.0e-30) / oraclePower);
			const auto below = db(powerBelow(difference, rate, audibleLimitHz), meanPower(outputs[0], margin));
			const auto prefix = juce::String(fixture.name) + "-" + juce::String(static_cast<int>(rate));
			if (!writeWav(directory.getChildFile(prefix + "-oracle.wav"), outputs[0], rate, playbackGain)
				|| !writeWav(directory.getChildFile(prefix + "-4bpo.wav"), outputs[1], rate, playbackGain * static_cast<float>(gain4))
				|| !writeWav(directory.getChildFile(prefix + "-2bpo.wav"), outputs[2], rate, playbackGain * static_cast<float>(gain2))
				|| !writeWav(directory.getChildFile(prefix + "-diff-4bpo.wav"), difference, rate, playbackGain)) return 1;
			csv << prefix << ',' << rate << ',' << fixture.morph << ',' << fixture.width << ',' << startHz << ',' << endHz
				<< ',' << model4 << ',' << model2 << ',' << measured << ',' << below << ',' << darkened << ',' << gain4 << ',' << gain2 << '\n';
			std::cout << prefix << " pitch=" << startHz << ".." << endHz << " model_4bpo=" << model4
				<< " model_2bpo=" << model2 << " measured_4bpo_file=" << measured << " below_15k=" << below << " darkened_4bpo_hz=" << darkened << '\n';
		}
	// Ladder set at 44.1 kHz (lowest darkening frequency): fairly open, moderate resonance.
	const auto ladderDirectory = directory.getChildFile("ladder");
	if (!ladderDirectory.createDirectory()) return 1;
	std::ofstream ladderCsv(ladderDirectory.getChildFile("ladder.csv").getFullPathName().toStdString());
	if (!ladderCsv) return 1;
	ladderCsv << std::setprecision(8) << "file,factor,drive_db,resonance,cutoff_hz,start_hz,end_hz,diff_total_dbc,diff_below_15k_dbc,"
		"diff_2bpo_below_15k_dbc,static_worst_partial_change_below_15k_db,match_gain_4bpo,match_gain_2bpo\n";
	constexpr double ladderRate = 44'100.0;
	constexpr float ladderResonance = 0.4f;
	for (const auto index : { 4, 0, 3, 2, 9 }) // static high square; square, 5% square, Morph 2.5 / 90% and two-tooth saw glides
	{
		const auto& fixture = fixtures[static_cast<std::size_t>(index)];
		auto coefficients = vekt::audio_lab::monoWidthShippedCoefficients(fixture.morph, fixture.width);
		coefficients.resize(topHarmonic + 1);
		auto startHz = fixture.startHz, endHz = fixture.endHz;
		if (fixture.worstStatic)
		{
			auto worst = -400.0;
			for (auto pitch = fixture.startHz; pitch <= fixture.endHz; pitch *= std::exp2(1.0 / 192.0))
			{
				const auto error = vekt::audio_lab::monoWidthLevelError(coefficients, fourBands, pitch, ladderRate);
				if (const auto value = db(error.errorPower, error.signalPower); value > worst) { worst = value; startHz = endHz = pitch; }
			}
		}
		for (const auto factor : { 1, 4 })
		{
			const auto sources = synthesize(coefficients, { nullptr, &fourBands, &twoBands }, ladderRate, startHz, endHz,
				fixture.seconds, factor);
			for (const auto drive : { 0.0f, 12.0f, 24.0f })
			{
				std::array<std::vector<float>, 3> outputs;
				for (std::size_t variant = 0; variant < 3; ++variant)
					outputs[variant] = throughLadder(sources[variant], ladderRate, factor, drive, ladderResonance);
				// Skip the first 50 ms: decimator latency and ladder start-up.
				const auto margin = static_cast<std::size_t>(0.05 * ladderRate);
				std::array<std::vector<float>, 2> differences;
				for (std::size_t variant = 0; variant < 2; ++variant)
				{
					differences[variant].resize(outputs[0].size());
					for (std::size_t sample = 0; sample < outputs[0].size(); ++sample)
						differences[variant][sample] = outputs[variant + 1][sample] - outputs[0][sample];
				}
				const auto oraclePower = meanPower(outputs[0], margin);
				const auto trimmed = [&](const std::vector<float>& samples)
				{
					return std::vector<float>(samples.begin() + static_cast<std::ptrdiff_t>(margin),
						samples.end() - static_cast<std::ptrdiff_t>(margin));
				};
				const auto total = db(meanPower(differences[0], margin), oraclePower);
				const auto below4 = db(powerBelow(trimmed(differences[0]), ladderRate, audibleLimitHz), oraclePower);
				const auto below2 = db(powerBelow(trimmed(differences[1]), ladderRate, audibleLimitHz), oraclePower);
				const auto partialChange = fixture.worstStatic
					? worstPartialLevelChangeDb(outputs[0], outputs[1], ladderRate, startHz, audibleLimitHz) : 0.0;
				const auto gain4 = rms(outputs[0]) / rms(outputs[1]);
				const auto gain2 = rms(outputs[0]) / rms(outputs[2]);
				const auto prefix = juce::String(fixture.name) + "-44100-" + juce::String(factor) + "x-drive"
					+ juce::String(static_cast<int>(drive));
				if (!writeWav(ladderDirectory.getChildFile(prefix + "-oracle.wav"), outputs[0], ladderRate, playbackGain)
					|| !writeWav(ladderDirectory.getChildFile(prefix + "-4bpo.wav"), outputs[1], ladderRate, playbackGain * static_cast<float>(gain4))
					|| !writeWav(ladderDirectory.getChildFile(prefix + "-2bpo.wav"), outputs[2], ladderRate, playbackGain * static_cast<float>(gain2))
					|| !writeWav(ladderDirectory.getChildFile(prefix + "-diff-4bpo.wav"), differences[0], ladderRate, playbackGain)) return 1;
				ladderCsv << prefix << ',' << factor << ',' << drive << ',' << ladderResonance << ",12000," << startHz << ',' << endHz
					<< ',' << total << ',' << below4 << ',' << below2 << ',' << partialChange << ',' << gain4 << ',' << gain2 << '\n';
				std::cout << "ladder " << prefix << " diff_total=" << total << " diff_below_15k=" << below4
					<< " diff_2bpo_below_15k=" << below2 << " static_partial_change_db=" << partialChange << '\n';
			}
		}
	}
	return 0;
}
