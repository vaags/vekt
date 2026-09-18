#include "SignalSources.h"

#include <PluginProcessor.h>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <iostream>
#include <memory>
#include <optional>
#include <string_view>

namespace
{
struct Options final
{
	vekt::audio_lab::Source source { vekt::audio_lab::Source::sine };
	double sampleRate { 48'000.0 };
	int blockSize { 128 };
	double seconds { 1.0 };
	double warmupSeconds { 0.2 };
	double frequencyHz { 1'000.0 };
	int mode {};
	int qualityIndex { -1 };
	int spectrumSize {};
	bool offlineProfile { true };
	float drive { 6.0f };
	float bias {};
	float shape { 0.5f };
	float dynamics { 0.5f };
	float texture { 0.5f };
	float tone {};
	float mix { 100.0f };
	float lowBandMix { 100.0f };
	float midBandMix { 100.0f };
	float highBandMix { 100.0f };
	bool autoGain {};
	std::uint32_t seed { 0x6d2b79f5u };
	juce::String reportPath;
	juce::String wavPath;
};

[[nodiscard]] bool valueFor(int& index, int argc, char** argv, std::string_view name, std::string& value)
{
	if (std::string_view(argv[index]) != name || index + 1 >= argc)
		return false;
	value = argv[++index];
	return true;
}

[[nodiscard]] bool parseOptions(int argc, char** argv, Options& options)
{
	try
	{
		for (int index = 1; index < argc; ++index)
		{
			std::string value;
			if (valueFor(index, argc, argv, "--source", value))
			{
				if (value == "sine") options.source = vekt::audio_lab::Source::sine;
				else if (value == "sawtooth") options.source = vekt::audio_lab::Source::sawtooth;
				else if (value == "sweep") options.source = vekt::audio_lab::Source::sweep;
				else if (value == "impulse") options.source = vekt::audio_lab::Source::impulse;
				else if (value == "noise") options.source = vekt::audio_lab::Source::noise;
				else if (value == "kick") options.source = vekt::audio_lab::Source::kick;
				else if (value == "unison") options.source = vekt::audio_lab::Source::unison;
				else if (value == "two-tone") options.source = vekt::audio_lab::Source::twoTone;
				else return false;
			}
			else if (valueFor(index, argc, argv, "--sample-rate", value)) options.sampleRate = std::stod(value);
			else if (valueFor(index, argc, argv, "--block-size", value)) options.blockSize = std::stoi(value);
			else if (valueFor(index, argc, argv, "--seconds", value)) options.seconds = std::stod(value);
			else if (valueFor(index, argc, argv, "--warmup", value)) options.warmupSeconds = std::stod(value);
			else if (valueFor(index, argc, argv, "--frequency", value)) options.frequencyHz = std::stod(value);
			else if (valueFor(index, argc, argv, "--mode", value)) options.mode = std::clamp(std::stoi(value), 0, 3);
			else if (valueFor(index, argc, argv, "--quality", value)) options.qualityIndex = std::clamp(std::stoi(value), 0, 6);
			else if (valueFor(index, argc, argv, "--spectrum-size", value)) options.spectrumSize = std::stoi(value);
			else if (valueFor(index, argc, argv, "--profile", value))
			{
				if (value == "offline") options.offlineProfile = true;
				else if (value == "tracking") options.offlineProfile = false;
				else return false;
			}
			else if (valueFor(index, argc, argv, "--drive", value)) options.drive = std::stof(value);
			else if (valueFor(index, argc, argv, "--bias", value)) options.bias = std::stof(value);
			else if (valueFor(index, argc, argv, "--shape", value)) options.shape = std::stof(value);
			else if (valueFor(index, argc, argv, "--dynamics", value)) options.dynamics = std::stof(value);
			else if (valueFor(index, argc, argv, "--texture", value)) options.texture = std::stof(value);
			else if (valueFor(index, argc, argv, "--tone", value)) options.tone = std::stof(value);
			else if (valueFor(index, argc, argv, "--mix", value)) options.mix = std::stof(value);
			else if (valueFor(index, argc, argv, "--low-mix", value)) options.lowBandMix = std::stof(value);
			else if (valueFor(index, argc, argv, "--mid-mix", value)) options.midBandMix = std::stof(value);
			else if (valueFor(index, argc, argv, "--high-mix", value)) options.highBandMix = std::stof(value);
			else if (valueFor(index, argc, argv, "--seed", value)) options.seed = static_cast<std::uint32_t>(std::stoul(value));
			else if (valueFor(index, argc, argv, "--report", value)) options.reportPath = value;
			else if (valueFor(index, argc, argv, "--wav", value)) options.wavPath = value;
			else if (std::string_view(argv[index]) == "--auto-gain") options.autoGain = true;
			else return false;
		}
	}
	catch (const std::exception&)
	{
		return false;
	}
	return options.sampleRate > 0.0 && options.blockSize > 0 && options.seconds > 0.0
		&& options.warmupSeconds >= 0.0 && options.frequencyHz > 0.0
		&& (options.spectrumSize == 0 || (options.spectrumSize > 1
			&& std::has_single_bit(static_cast<unsigned int>(options.spectrumSize))));
}

void setParameter(vekt::rav::PluginProcessor& processor, const char* identifier, float value)
{
	if (auto* parameter = processor.getParameters().getParameter(identifier))
		parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

[[nodiscard]] juce::String qualityName(vekt::dsp::OversamplingQuality quality)
{
	if (quality.factor == vekt::dsp::OversamplingFactor::off)
		return "Off";
	return juce::String(static_cast<int>(quality.multiplier())) + "x "
		+ (quality.filter == vekt::dsp::OversamplingFilter::polyphaseFIR ? "FIR" : "IIR");
}

[[nodiscard]] juce::String sourceName(vekt::audio_lab::Source source)
{
	switch (source)
	{
		case vekt::audio_lab::Source::sine: return "sine";
		case vekt::audio_lab::Source::sawtooth: return "sawtooth";
		case vekt::audio_lab::Source::sweep: return "sweep";
		case vekt::audio_lab::Source::impulse: return "impulse";
		case vekt::audio_lab::Source::noise: return "noise";
		case vekt::audio_lab::Source::kick: return "kick";
		case vekt::audio_lab::Source::unison: return "unison";
		case vekt::audio_lab::Source::twoTone: return "two-tone";
	}

	return {};
}
}

int main(int argc, char** argv)
{
	juce::ScopedJuceInitialiser_GUI juceInitialiser;
	Options options;
	if (!parseOptions(argc, argv, options))
	{
		std::cerr << "Usage: VektRavRender [--source sine|sawtooth|sweep|impulse|noise|kick|unison|two-tone] "
					 "[--profile tracking|offline] [--quality 0-6] [--sample-rate Hz] [--block-size samples] "
					 "[--seconds duration] [--warmup duration] [--frequency Hz] [--mode 0-3] "
					 "[--drive dB] [--bias value] [--shape value] [--dynamics value] [--texture value] "
					 "[--tone dB] [--mix percent] [--low-mix percent] [--mid-mix percent] "
					 "[--high-mix percent] [--auto-gain] [--seed value] [--report path] [--wav path]\n";
		return 64;
	}

	vekt::rav::PluginProcessor processor;
	setParameter(processor, vekt::rav::parameters::mode, static_cast<float>(options.mode));
	setParameter(processor, vekt::rav::parameters::drive, options.drive);
	setParameter(processor, vekt::rav::parameters::bias, options.bias);
	setParameter(processor, vekt::rav::parameters::shape, options.shape);
	setParameter(processor, vekt::rav::parameters::dynamics, options.dynamics);
	setParameter(processor, vekt::rav::parameters::texture, options.texture);
	setParameter(processor, vekt::rav::parameters::tone, options.tone);
	setParameter(processor, vekt::rav::parameters::mix, options.mix);
	setParameter(processor, vekt::rav::parameters::lowBandMix, options.lowBandMix);
	setParameter(processor, vekt::rav::parameters::midBandMix, options.midBandMix);
	setParameter(processor, vekt::rav::parameters::highBandMix, options.highBandMix);
	setParameter(processor, vekt::rav::parameters::autoGain, options.autoGain ? 1.0f : 0.0f);
	if (options.qualityIndex >= 0)
		setParameter(processor, options.offlineProfile ? vekt::rav::parameters::offlineOversampling
			: vekt::rav::parameters::trackingOversampling, static_cast<float>(options.qualityIndex));
	processor.setNonRealtime(options.offlineProfile);
	processor.prepareToPlay(options.sampleRate, options.blockSize);

	const auto warmupSamples = static_cast<std::int64_t>(options.sampleRate * options.warmupSeconds);
	const auto totalSamples = static_cast<std::int64_t>(options.sampleRate * options.seconds);
	vekt::audio_lab::SignalSource source(options.source, options.sampleRate, options.seed);
	source.setFrequency(options.frequencyHz);
	juce::MidiBuffer midi;
	double sumSquares = 0.0;
	double sum = 0.0;
	double processTicks = 0.0;
	float peak = 0.0f;
	std::vector<float> spectrumSamples;
	spectrumSamples.reserve(static_cast<std::size_t>(options.spectrumSize));
	juce::WavAudioFormat wavFormat;
	std::unique_ptr<juce::AudioFormatWriter> wavWriter;
	if (options.wavPath.isNotEmpty())
	{
		std::unique_ptr<juce::OutputStream> stream = juce::File(options.wavPath).createOutputStream();
		wavWriter = wavFormat.createWriterFor(stream, juce::AudioFormatWriterOptions {}
			.withSampleRate(options.sampleRate)
			.withNumChannels(2)
			.withBitsPerSample(32));
		if (wavWriter == nullptr)
		{
			std::cerr << "Unable to create WAV output\n";
			return 1;
		}
	}
	const auto renderBlock = [&](std::int64_t offset, int blockSize, bool measure) -> bool
	{
		juce::AudioBuffer<float> buffer(2, blockSize);
		for (int sample = 0; sample < blockSize; ++sample)
		{
			buffer.setSample(0, sample, source.next(offset + sample, 0));
			buffer.setSample(1, sample, source.next(offset + sample, 1));
		}
		const auto startTicks = juce::Time::getHighResolutionTicks();
		processor.processBlock(buffer, midi);
		if (!measure)
			return true;
		processTicks += static_cast<double>(juce::Time::getHighResolutionTicks() - startTicks);
		for (int sample = 0; sample < blockSize; ++sample)
		{
			const auto value = buffer.getSample(0, sample);
			sumSquares += static_cast<double>(value) * value;
			sum += value;
			peak = std::max(peak, std::abs(value));
			if (spectrumSamples.size() < static_cast<std::size_t>(options.spectrumSize))
				spectrumSamples.push_back(value);
		}
		if (wavWriter != nullptr && !wavWriter->writeFromAudioSampleBuffer(buffer, 0, blockSize))
		{
			std::cerr << "Unable to write WAV output\n";
			return false;
		}
		return true;
	};
	for (std::int64_t offset = 0; offset < warmupSamples; offset += options.blockSize)
		if (!renderBlock(offset, static_cast<int>(std::min<std::int64_t>(options.blockSize,
				warmupSamples - offset)), false))
			return 1;
	for (std::int64_t offset = 0; offset < totalSamples; offset += options.blockSize)
		if (!renderBlock(warmupSamples + offset, static_cast<int>(std::min<std::int64_t>(options.blockSize,
				totalSamples - offset)), true))
			return 1;

	const auto rms = std::sqrt(sumSquares / static_cast<double>(totalSamples));
	const auto dc = sum / static_cast<double>(totalSamples);
	const auto processingSeconds = processTicks
		/ static_cast<double>(juce::Time::getHighResolutionTicksPerSecond());
	const auto cpuPercent = 100.0 * processingSeconds / options.seconds;
	const auto nanosecondsPerSample = 1.0e9 * processingSeconds / static_cast<double>(totalSamples);
	std::optional<double> spectrumPeakDbfs;
	if (options.spectrumSize > 0
		&& spectrumSamples.size() == static_cast<std::size_t>(options.spectrumSize))
	{
		const auto size = spectrumSamples.size();
			double windowSum = 0.0;
		std::vector<float> fftData(size * 2);
		for (std::size_t sample = 0; sample < size; ++sample)
		{
			const auto window = 0.5 - 0.5 * std::cos(2.0 * juce::MathConstants<double>::pi
				* static_cast<double>(sample) / static_cast<double>(size - 1));
				windowSum += window;
			fftData[sample] = static_cast<float>(static_cast<double>(spectrumSamples[sample]) * window);
		}
		juce::dsp::FFT(std::countr_zero(static_cast<unsigned int>(size)))
			.performFrequencyOnlyForwardTransform(fftData.data(), true);
		const auto peakMagnitude = *std::max_element(fftData.cbegin() + 1,
			fftData.cbegin() + static_cast<std::ptrdiff_t>(size / 2));
		spectrumPeakDbfs = juce::Decibels::gainToDecibels(
			2.0 * static_cast<double>(peakMagnitude) / windowSum, -200.0);
	}
	std::cout << "samples=" << totalSamples << " rms=" << rms
		<< " peak=" << peak << " dc=" << dc << " latency=" << processor.getLatencySamples()
		<< " quality=" << qualityName(processor.getActiveQuality())
		<< " cpu_percent=" << cpuPercent << " ns_per_sample=" << nanosecondsPerSample;
	if (spectrumPeakDbfs)
		std::cout << " spectrum_peak_dbfs=" << *spectrumPeakDbfs;
	std::cout << '\n';
	if (options.reportPath.isNotEmpty())
	{
		const auto report = "{\n"
			"  \"samples\": " + juce::String(totalSamples) + ",\n"
			"  \"sample_rate\": " + juce::String(options.sampleRate) + ",\n"
			"  \"block_size\": " + juce::String(options.blockSize) + ",\n"
			"  \"warmup_seconds\": " + juce::String(options.warmupSeconds) + ",\n"
			"  \"source\": " + juce::JSON::toString(sourceName(options.source)) + ",\n"
			"  \"frequency_hz\": " + juce::String(options.frequencyHz) + ",\n"
			"  \"seed\": " + juce::String(static_cast<juce::int64>(options.seed)) + ",\n"
			"  \"mode\": " + juce::String(options.mode) + ",\n"
			"  \"profile\": " + juce::JSON::toString(options.offlineProfile ? "offline" : "tracking") + ",\n"
			"  \"requested_quality_index\": " + juce::String(options.qualityIndex) + ",\n"
			"  \"quality\": " + juce::JSON::toString(qualityName(processor.getActiveQuality())) + ",\n"
			"  \"parameters\": {\n"
			"    \"drive_db\": " + juce::String(options.drive) + ",\n"
			"    \"bias\": " + juce::String(options.bias) + ",\n"
			"    \"shape\": " + juce::String(options.shape) + ",\n"
			"    \"dynamics\": " + juce::String(options.dynamics) + ",\n"
			"    \"texture\": " + juce::String(options.texture) + ",\n"
			"    \"tone_db\": " + juce::String(options.tone) + ",\n"
			"    \"mix_percent\": " + juce::String(options.mix) + ",\n"
			"    \"low_mix_percent\": " + juce::String(options.lowBandMix) + ",\n"
			"    \"mid_mix_percent\": " + juce::String(options.midBandMix) + ",\n"
			"    \"high_mix_percent\": " + juce::String(options.highBandMix) + ",\n"
			"    \"auto_gain\": " + juce::String(options.autoGain ? "true" : "false") + "\n"
			"  },\n"
			"  \"latency_samples\": " + juce::String(processor.getLatencySamples()) + ",\n"
			"  \"rms\": " + juce::String(rms, 12) + ",\n"
			"  \"peak\": " + juce::String(peak, 12) + ",\n"
			"  \"dc\": " + juce::String(dc, 12) + ",\n"
			"  \"cpu_percent\": " + juce::String(cpuPercent, 12) + ",\n"
			"  \"nanoseconds_per_sample\": " + juce::String(nanosecondsPerSample, 12) + ",\n"
			"  \"spectrum_peak_dbfs\": " + (spectrumPeakDbfs
				? juce::String(*spectrumPeakDbfs, 12) : juce::String("null")) + "\n"
			"}\n";
		if (!juce::File(options.reportPath).replaceWithText(report))
		{
			std::cerr << "Unable to write report output\n";
			return 1;
		}
	}
	return 0;
}
