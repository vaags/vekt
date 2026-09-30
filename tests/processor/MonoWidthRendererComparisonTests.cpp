#include "MonoVoice.h"
#include "../../tools/audio_lab/MonoWidthReference.h"
#include "../../tools/audio_lab/MonoWidthLongResidual.h"
#include "../../tools/audio_lab/MonoWidthTablePrototype.h"
#include "../../tools/audio_lab/MonoWidthAdaptiveKnots.h"
#include "../../tools/audio_lab/MonoWidthPitchLevels.h"
#include "../../tools/audio_lab/MonoAdditiveOscillator.h"

#include <vekt/dsp/OversamplingBank.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
#include <numbers>
#include <numeric>
#include <tuple>
#include <vector>

namespace
{
constexpr int hostRate = 48'000;
constexpr int fftSize = 2'048;
constexpr int warmup = fftSize;
constexpr int blockSize = 256;

enum class Renderer { raw, reference, longResidual, table };


vekt::dsp::OversamplingQuality quality(int factor)
{
	using namespace vekt::dsp;
	switch (factor)
	{
	case 2: return { OversamplingFactor::x2, OversamplingFilter::polyphaseIIR };
	case 4: return { OversamplingFactor::x4, OversamplingFilter::polyphaseFIR };
	case 8: return { OversamplingFactor::x8, OversamplingFilter::polyphaseFIR };
	default: return { OversamplingFactor::off, OversamplingFilter::polyphaseIIR };
	}
}

// Identical downsampling/latency for source and ideal reference. There is no
// upsampling of a host-rate signal: the oscillator generates at its own rate.
std::vector<double> render(Renderer renderer, int factor, int frequencyBin, float morph, float width,
	const std::vector<std::complex<double>>& harmonics, const vekt::audio_lab::MonoWidthLongResidual* residual = nullptr,
	const vekt::audio_lab::MonoWidthTablePrototype* table = nullptr,
	vekt::audio_lab::MonoWidthTablePrototype::PhaseInterpolation interpolation = vekt::audio_lab::MonoWidthTablePrototype::PhaseInterpolation::linear,
	vekt::audio_lab::MonoWidthTablePrototype::WidthInterpolation widthInterpolation = vekt::audio_lab::MonoWidthTablePrototype::WidthInterpolation::linear)
{
	vekt::dsp::OversamplingBank<float> bank(1);
	bank.prepare(blockSize);
	bank.activate(quality(factor));
	juce::AudioBuffer<float> buffer(1, blockSize);
	std::vector<double> output;
	output.reserve(fftSize);
	const auto frequency = static_cast<double>(frequencyBin) * hostRate / fftSize;
	for (int start = 0; start < warmup + fftSize; start += blockSize)
	{
		buffer.clear();
		juce::dsp::AudioBlock<float> host(buffer);
		const juce::dsp::AudioBlock<const float> input(host);
		auto internal = bank.processSamplesUp(input);
		for (int index = 0; index < blockSize * factor; ++index)
		{
			const auto phase = std::fmod(static_cast<double>(start * factor + index) * frequency
				/ (hostRate * factor), 1.0);
			float value {};
			if (renderer == Renderer::reference)
			{
				double ideal = harmonics[0].real();
				const auto unit = std::polar(1.0, 2.0 * std::numbers::pi * phase);
				auto oscillator = unit;
				for (std::size_t harmonic = 1; harmonic < harmonics.size(); ++harmonic)
				{
					ideal += 2.0 * (harmonics[harmonic] * oscillator).real();
					oscillator *= unit;
				}
				value = static_cast<float>(ideal);
			}
			else if (renderer == Renderer::raw)
				value = static_cast<float>(vekt::audio_lab::monoWidthIdealWave(phase, morph, width));
			else if (renderer == Renderer::longResidual)
				value = static_cast<float>(residual->wave(phase, frequency / (hostRate * factor), morph, width));
			else
				value = static_cast<float>(table->wave(phase, morph, width, frequency, interpolation, widthInterpolation));
			internal.setSample(0, index, value);
		}
		bank.processSamplesDown(host);
		if (start >= warmup)
			for (int index = 0; index < blockSize; ++index)
				output.push_back(buffer.getSample(0, index));
	}
	return output;
}

std::vector<std::complex<double>> spectrum(const std::vector<double>& samples)
{
	juce::dsp::FFT fft(11);
	std::vector<juce::dsp::Complex<float>> input(fftSize), output(fftSize);
	for (int index = 0; index < fftSize; ++index)
		input[static_cast<std::size_t>(index)] = { static_cast<float>(samples[static_cast<std::size_t>(index)]), 0.0f };
	fft.perform(input.data(), output.data(), false);
	std::vector<std::complex<double>> bins(fftSize / 2 + 1);
	for (std::size_t index = 0; index < bins.size(); ++index)
		bins[index] = { output[index].real() / fftSize, output[index].imag() / fftSize };
	return bins;
}

struct Score
{
	double harmonicError {}, aliasPower {}, dcError {}, rmsError {}, timeError {}, signalPower {};
};

Score score(const std::vector<double>& candidate, const std::vector<double>& reference, int frequencyBin)
{
	const auto actual = spectrum(candidate);
	const auto target = spectrum(reference);
	Score result;
	double actualPower {}, targetPower {};
	for (int index = 0; index < fftSize; ++index)
	{
		const auto value = candidate[static_cast<std::size_t>(index)];
		const auto ideal = reference[static_cast<std::size_t>(index)];
		result.timeError += (value - ideal) * (value - ideal) / fftSize;
		actualPower += value * value / fftSize;
		targetPower += ideal * ideal / fftSize;
	}
	result.rmsError = std::sqrt(actualPower) - std::sqrt(targetPower);
	result.dcError = actual[0].real() - target[0].real();
	result.signalPower = targetPower;
	for (int bin = 1; bin < fftSize / 2; ++bin)
	{
		const auto power = 2.0 * std::norm(actual[static_cast<std::size_t>(bin)]);
		if (bin % frequencyBin == 0)
			result.harmonicError += 2.0 * std::norm(actual[static_cast<std::size_t>(bin)] - target[static_cast<std::size_t>(bin)]);
		else result.aliasPower += power;
	}
	return result;
}

double db(double power, double referencePower)
{
	return 10.0 * std::log10(std::max(power, 1.0e-30) / std::max(referencePower, 1.0e-30));
}
}

TEST_CASE("Mono Width comparison separates harmonic phase error from folded spur power",
	"[mono][oscillator][width][comparison]")
{
	std::vector<double> reference(fftSize), candidate(fftSize);
	constexpr int fundamentalBin = 37, spurBin = 200;
	for (int sample = 0; sample < fftSize; ++sample)
	{
		const auto phase = 2.0 * std::numbers::pi * sample / fftSize;
		reference[static_cast<std::size_t>(sample)] = 0.5 * std::sin(fundamentalBin * phase);
		candidate[static_cast<std::size_t>(sample)] = 0.5 * std::cos(fundamentalBin * phase)
			+ 0.125 * std::sin(spurBin * phase) + 0.1;
	}
	const auto clean = score(reference, reference, fundamentalBin);
	REQUIRE(clean.harmonicError < 1.0e-20);
	REQUIRE(clean.timeError < 1.0e-20);
	const auto measured = score(candidate, reference, fundamentalBin);
	REQUIRE(measured.harmonicError == Catch::Approx(0.25).margin(1.0e-6));
	REQUIRE(measured.aliasPower == Catch::Approx(0.125 * 0.125 * 0.5).margin(1.0e-6));
	REQUIRE(measured.dcError == Catch::Approx(0.1).margin(1.0e-6));
}

TEST_CASE("Mono Width event locations and one-sided derivatives match the ideal geometry",
	"[mono][oscillator][width][comparison]")
{
	using vekt::audio_lab::monoWidthIdealAnchor;
	constexpr double epsilon = 1.0e-6;
	for (const auto width : { 5.0, 10.0, 20.0, 49.0, 51.0, 80.0, 95.0 })
	{
		const auto d = width * 0.01;
		const auto leftSlope = 0.5 / d, rightSlope = 0.5 / (1.0 - d);
		for (int anchor = 0; anchor < 4; ++anchor)
		{
			CAPTURE(width, anchor);
			const auto check = [&](double phase, double valueJump, double slopeJump)
			{
				const auto leftPhase = phase == 0.0 ? 1.0 - epsilon : phase - epsilon;
				const auto rightPhase = phase + epsilon;
				const auto left2Phase = phase == 0.0 ? 1.0 - 2.0 * epsilon : phase - 2.0 * epsilon;
				const auto at = [&](double p) { return monoWidthIdealAnchor(anchor, p, width); };
				const auto jump = at(rightPhase) - at(leftPhase);
				const auto derivativeJump = (at(rightPhase + epsilon) - at(rightPhase)) / epsilon
					- (at(leftPhase) - at(left2Phase)) / epsilon;
				REQUIRE(jump == Catch::Approx(valueJump).margin(1.0e-3));
				REQUIRE(derivativeJump == Catch::Approx(slopeJump).margin(2.0e-3));
			};
			switch (anchor)
			{
			case 0:
				check(d, 0.0, -2.0 * std::numbers::pi * vekt::mono::widthSineGain * (rightSlope - leftSlope));
				check(0.0, 0.0, 2.0 * std::numbers::pi * vekt::mono::widthSineGain * (leftSlope - rightSlope));
				break;
			case 1:
				check(d * 0.5, 0.0, -8.0 * leftSlope);
				check((1.0 + d) * 0.5, 0.0, 8.0 * rightSlope);
				check(d, 0.0, -4.0 * (rightSlope - leftSlope));
				check(0.0, 0.0, 4.0 * (leftSlope - rightSlope));
				break;
			case 2:
				check(d, 0.0, -2.0 * (rightSlope - leftSlope));
				check(0.0, 2.0, -2.0 * (leftSlope - rightSlope));
				break;
			default:
				check(d, -2.0 * vekt::mono::widthPulseGain, 0.0);
				check(0.0, 2.0 * vekt::mono::widthPulseGain, 0.0);
				break;
			}
		}
	}
}

TEST_CASE("Mono Width offline long windowed sinc event residual convergence",
	"[mono][oscillator][width][comparison][slow]")
{
	constexpr std::array radii { 8, 16, 32, 64 };
	std::array<vekt::audio_lab::MonoWidthLongResidual, 4> kernels {
		vekt::audio_lab::MonoWidthLongResidual(8), vekt::audio_lab::MonoWidthLongResidual(16),
		vekt::audio_lab::MonoWidthLongResidual(32), vekt::audio_lab::MonoWidthLongResidual(64) };
	for (const auto factor : { 1, 2, 4, 8 })
	{
		std::array<double, 4> worstAlias {}, worstHarmonic {}, worstDc {}, worstRms {};
		worstAlias.fill(-300.0); worstHarmonic.fill(-300.0);
		std::array<int, 4> aliasWins {};
		int points {};
		for (const auto frequencyBin : { 37, 151 })
			for (const auto width : { 5.0f, 10.0f, 20.0f, 49.9f, 50.0f, 50.1f, 80.0f, 95.0f })
				for (const auto morph : { 0.0f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f })
				{
					CAPTURE(factor, frequencyBin, width, morph);
					const auto frequency = static_cast<double>(frequencyBin) * hostRate / fftSize;
					const auto reference = vekt::audio_lab::renderMonoWidthReference(morph, width, frequency, hostRate, 0);
					const auto ideal = render(Renderer::reference, factor, frequencyBin, morph, width, reference.harmonics);
					const auto raw = score(render(Renderer::raw, factor, frequencyBin, morph, width, reference.harmonics), ideal, frequencyBin);
					++points;
					for (std::size_t index = 0; index < radii.size(); ++index)
					{
						const auto result = score(render(Renderer::longResidual, factor, frequencyBin, morph, width,
							reference.harmonics, &kernels[index]), ideal, frequencyBin);
						REQUIRE(std::isfinite(result.timeError));
						worstAlias[index] = std::max(worstAlias[index], db(result.aliasPower, result.signalPower));
						worstHarmonic[index] = std::max(worstHarmonic[index], db(result.harmonicError, result.signalPower));
						worstDc[index] = std::max(worstDc[index], std::abs(result.dcError));
						worstRms[index] = std::max(worstRms[index], std::abs(result.rmsError));
						aliasWins[index] += result.aliasPower < raw.aliasPower;
					}
				}
		for (std::size_t index = 0; index < radii.size(); ++index)
		{
			std::cout << "long-residual factor=" << factor << " radius=" << radii[index] << " points=" << points
				<< " worst_alias=" << worstAlias[index] << " worst_harmonic=" << worstHarmonic[index]
				<< " max_abs_dc=" << worstDc[index] << " max_abs_rms=" << worstRms[index]
				<< " alias_vs_raw=" << aliasWins[index] << '\n';
			REQUIRE(points == 96);
		}
	}
}

TEST_CASE("Mono Width Fourier table reconstructs its source coefficients and band limits",
	"[mono][oscillator][width][comparison]")
{
	using Table = vekt::audio_lab::MonoWidthTablePrototype;
	const auto low = 37.0 * hostRate / fftSize, high = 151.0 * hostRate / fftSize;
	const Table bank({ 5.0, 50.0, 95.0 }, 10, hostRate, hostRate, { low, high });
	REQUIRE(bank.harmonicLimit(low, hostRate, hostRate) == 27);
	REQUIRE(bank.harmonicLimit(high, hostRate, hostRate) == 6);
	for (const auto frequencyBin : { 37, 151 })
		for (const auto width : { 5.0f, 50.0f, 95.0f })
			for (const auto morph : { 0.0f, 1.0f, 2.0f, 3.0f })
			{
				CAPTURE(frequencyBin, width, morph);
				const auto frequency = static_cast<double>(frequencyBin) * hostRate / fftSize;
				const auto reference = vekt::audio_lab::renderMonoWidthReference(morph, width, frequency, hostRate, 0);
				for (const auto phase : { 0.0, 0.0625, 0.25, 0.5, 0.875 })
				{
					double ideal = reference.harmonics[0].real();
					for (std::size_t harmonic = 1; harmonic < reference.harmonics.size(); ++harmonic)
						ideal += 2.0 * (reference.harmonics[harmonic]
							* std::polar(1.0, 2.0 * std::numbers::pi * static_cast<double>(harmonic) * phase)).real();
					REQUIRE(bank.wave(phase, morph, width, frequency, Table::PhaseInterpolation::cubic)
						== Catch::Approx(ideal).margin(2.0e-5));
				}
			}
}

TEST_CASE("Mono Width offline Fourier table spacing size and phase interpolation sweep",
	"[mono][oscillator][width][comparison][slow]")
{
	using Table = vekt::audio_lab::MonoWidthTablePrototype;
	const auto low = 37.0 * hostRate / fftSize, high = 151.0 * hostRate / fftSize;
	struct Target
	{
		int frequencyBin, factor;
		float width, morph;
		std::vector<double> ideal;
		std::vector<std::complex<double>> bins;
	};
	std::vector<Target> targets;
	for (const auto frequencyBin : { 37, 151 })
		for (const auto width : { 5.0f, 10.0f, 20.0f, 35.0f, 49.9f, 50.0f, 50.1f, 65.0f, 80.0f, 90.0f, 95.0f })
			for (const auto morph : { 0.0f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f })
			{
				const auto frequency = static_cast<double>(frequencyBin) * hostRate / fftSize;
				const auto reference = vekt::audio_lab::renderMonoWidthReference(morph, width, frequency, hostRate, 0);
				for (const auto factor : { 1, 4 })
				{
					auto ideal = render(Renderer::reference, factor, frequencyBin, morph, width, reference.harmonics);
					targets.push_back({ frequencyBin, factor, width, morph, std::move(ideal), {} });
					targets.back().bins = spectrum(targets.back().ideal);
				}
			}
	for (const auto slices : { 17, 33 })
		for (const auto spacing : { Table::WidthSpacing::uniform, Table::WidthSpacing::logit })
			for (const auto order : { 10, 11, 12 })
			{
				const auto frames = Table::widths(slices, spacing);
				const Table bank(frames, order, hostRate * 4.0, hostRate, { low, high });
				for (const auto interpolation : { Table::PhaseInterpolation::linear, Table::PhaseInterpolation::cubic })
				{
					double worstAlias = -300.0, worstHarmonic = -300.0, maxDc {}, maxRms {}, maxFundamentalPhase {};
					int points {};
					for (const auto& target : targets)
					{
						const auto frequencyBin = target.frequencyBin, factor = target.factor;
						const auto width = target.width, morph = target.morph;
						// This bank was generated against 4x internal Nyquist;
						// both tested qualities share the stricter host cutoff.
						const auto candidate = render(Renderer::table, factor, frequencyBin, morph, width,
							{}, nullptr, &bank, interpolation);
						const auto measured = score(candidate, target.ideal, frequencyBin);
						const auto actualBins = spectrum(candidate);
						const auto fundamental = static_cast<std::size_t>(frequencyBin);
						const auto phaseError = std::abs(std::arg(actualBins[fundamental] / target.bins[fundamental]));
						maxFundamentalPhase = std::max(maxFundamentalPhase, phaseError);
						worstAlias = std::max(worstAlias, db(measured.aliasPower, measured.signalPower));
						worstHarmonic = std::max(worstHarmonic, db(measured.harmonicError, measured.signalPower));
						maxDc = std::max(maxDc, std::abs(measured.dcError));
						maxRms = std::max(maxRms, std::abs(measured.rmsError));
						++points;
						CAPTURE(slices, spacing, order, interpolation, factor, frequencyBin, width, morph);
						REQUIRE(std::isfinite(phaseError));
						REQUIRE(std::isfinite(measured.timeError));
					}
					std::cout << "table-sweep slices=" << slices << " spacing=" << (spacing == Table::WidthSpacing::uniform ? "uniform" : "logit")
						<< " size=" << (1 << order) << " lookup=" << (interpolation == Table::PhaseInterpolation::linear ? "linear" : "cubic")
						<< " bytes=" << bank.bytes() << " points=" << points << " worst_alias=" << worstAlias
						<< " worst_harmonic=" << worstHarmonic << " max_dc=" << maxDc << " max_rms=" << maxRms
						<< " max_fundamental_phase_rad=" << maxFundamentalPhase << '\n';
					REQUIRE(points == 264);
				}
			}
}

TEST_CASE("Mono Width offline Fourier table higher Width density and interpolation diagnostic",
	"[mono][oscillator][width][comparison][slow]")
{
	using Table = vekt::audio_lab::MonoWidthTablePrototype;
	const auto low = 37.0 * hostRate / fftSize, high = 151.0 * hostRate / fftSize;
	for (const auto slices : { 33, 65 })
		for (const auto spacing : { Table::WidthSpacing::uniform, Table::WidthSpacing::logit })
		{
			const Table bank(Table::widths(slices, spacing), 11, hostRate * 4.0, hostRate, { low, high });
			for (const auto widthInterpolation : { Table::WidthInterpolation::linear, Table::WidthInterpolation::cubic })
			{
				double worstHarmonic = -300.0, worstAlias = -300.0, maxDc {}, maxRms {}, maxPhase {};
				float worstWidth {}, worstMorph {};
				int worstBin {}, points {};
				for (const auto frequencyBin : { 37, 151 })
					for (const auto width : { 5.0f, 7.0f, 10.0f, 15.0f, 20.0f, 35.0f, 49.9f, 50.0f, 50.1f, 65.0f, 80.0f, 90.0f, 93.0f, 95.0f })
						for (const auto morph : { 0.0f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f })
						{
							const auto frequency = static_cast<double>(frequencyBin) * hostRate / fftSize;
							const auto reference = vekt::audio_lab::renderMonoWidthReference(morph, width, frequency, hostRate, 0);
							const auto ideal = render(Renderer::reference, 4, frequencyBin, morph, width, reference.harmonics);
							const auto candidate = render(Renderer::table, 4, frequencyBin, morph, width, {}, nullptr, &bank,
								Table::PhaseInterpolation::cubic, widthInterpolation);
							const auto result = score(candidate, ideal, frequencyBin);
							const auto harmonic = db(result.harmonicError, result.signalPower);
							if (harmonic > worstHarmonic)
							{
								worstHarmonic = harmonic;
								worstWidth = width; worstMorph = morph; worstBin = frequencyBin;
							}
							worstAlias = std::max(worstAlias, db(result.aliasPower, result.signalPower));
							maxDc = std::max(maxDc, std::abs(result.dcError));
							maxRms = std::max(maxRms, std::abs(result.rmsError));
							const auto actualBins = spectrum(candidate), idealBins = spectrum(ideal);
							maxPhase = std::max(maxPhase,
								std::abs(std::arg(actualBins[static_cast<std::size_t>(frequencyBin)] / idealBins[static_cast<std::size_t>(frequencyBin)])));
							CAPTURE(slices, spacing, widthInterpolation, frequencyBin, width, morph);
							REQUIRE(std::isfinite(result.timeError));
							++points;
						}
				std::cout << "table-width slices=" << slices << " spacing=" << (spacing == Table::WidthSpacing::uniform ? "uniform" : "logit")
					<< " width_lookup=" << (widthInterpolation == Table::WidthInterpolation::linear ? "linear" : "cubic")
					<< " bytes=" << bank.bytes() << " points=" << points << " worst_alias=" << worstAlias
					<< " worst_harmonic=" << worstHarmonic << " worst_at=" << worstBin << '/' << worstWidth << '/' << worstMorph
					<< " max_dc=" << maxDc << " max_rms=" << maxRms << " max_fundamental_phase_rad=" << maxPhase << '\n';
				REQUIRE(points == 168);
			}
		}
}

TEST_CASE("Mono Width offline Fourier table selected static neutral and phase regression",
	"[mono][oscillator][width][comparison][slow]")
{
	using Table = vekt::audio_lab::MonoWidthTablePrototype;
	const auto low = 37.0 * hostRate / fftSize, high = 151.0 * hostRate / fftSize;
	const Table bank(Table::widths(65, Table::WidthSpacing::uniform), 11,
		hostRate * 4.0, hostRate, { low, high });
	for (const auto factor : { 1, 4 })
		for (const auto frequencyBin : { 37, 151 })
			for (const auto morph : { 0.0f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f })
			{
				std::array<std::vector<double>, 3> outputs;
				for (std::size_t index = 0; index < outputs.size(); ++index)
				{
					const auto width = std::array { 49.9f, 50.0f, 50.1f }[index];
					const auto frequency = static_cast<double>(frequencyBin) * hostRate / fftSize;
					const auto reference = vekt::audio_lab::renderMonoWidthReference(morph, width, frequency, hostRate, 0);
					const auto ideal = render(Renderer::reference, factor, frequencyBin, morph, width, reference.harmonics);
					outputs[index] = render(Renderer::table, factor, frequencyBin, morph, width, {}, nullptr, &bank,
						Table::PhaseInterpolation::cubic, Table::WidthInterpolation::cubic);
					const auto result = score(outputs[index], ideal, frequencyBin);
					const auto actualBins = spectrum(outputs[index]), targetBins = spectrum(ideal);
					const auto fundamental = static_cast<std::size_t>(frequencyBin);
					CAPTURE(factor, frequencyBin, morph, width);
					REQUIRE(db(result.aliasPower, result.signalPower) < -110.0);
					REQUIRE(db(result.harmonicError, result.signalPower) < -40.0);
					REQUIRE(std::abs(result.dcError) < 0.0001);
					REQUIRE(std::abs(result.rmsError) < 0.001);
					REQUIRE(std::abs(std::arg(actualBins[fundamental] / targetBins[fundamental])) < 0.0002);
				}
				for (const auto neighbor : { 0, 2 })
				{
					double difference {};
					for (std::size_t sample = 0; sample < outputs[1].size(); ++sample)
						difference += std::pow(outputs[static_cast<std::size_t>(neighbor)][sample] - outputs[1][sample], 2.0)
							/ static_cast<double>(outputs[1].size());
					CAPTURE(factor, frequencyBin, morph, neighbor);
					std::cout << "table-neutral-adjacent factor=" << factor << " bin=" << frequencyBin << " morph=" << morph
						<< " neighbor=" << neighbor << " rms=" << std::sqrt(difference) << '\n';
					REQUIRE(std::isfinite(difference));
				}
			}
}

TEST_CASE("Mono Width mirror identity holds for Fourier coefficients and half-width table",
	"[mono][oscillator][width][comparison][slow]")
{
	using Table = vekt::audio_lab::MonoWidthTablePrototype;
	const auto low = 37.0 * hostRate / fftSize, high = 151.0 * hostRate / fftSize;
	const Table full(Table::widths(65, Table::WidthSpacing::uniform), 11, hostRate * 4.0, hostRate, { low, high });
	std::vector<double> half;
	for (const auto width : Table::widths(65, Table::WidthSpacing::uniform))
		if (width <= 50.000001) half.push_back(width);
	const Table mirrored(half, 11, hostRate * 4.0, hostRate, { low, high }, true);
	REQUIRE(half.size() == 33);
	REQUIRE(mirrored.bytes() * 65 == full.bytes() * 33);
	double worstCoefficient {}, worstTable {}, worstIdeal {}, worstFullOracle {}, worstMirroredOracle {};
	for (const auto bin : { 37, 151 })
		for (const auto width : { 5.0, 7.0, 10.0, 20.0, 35.0, 49.9, 50.0 })
			for (int anchor = 0; anchor < 4; ++anchor)
			{
				const auto frequency = static_cast<double>(bin) * hostRate / fftSize;
				const auto left = vekt::audio_lab::renderMonoWidthReference(anchor, width, frequency, hostRate, 0);
				const auto right = vekt::audio_lab::renderMonoWidthReference(anchor, 100.0 - width, frequency, hostRate, 0);
				CAPTURE(bin, width, anchor);
				REQUIRE(left.harmonics.size() == right.harmonics.size());
				for (std::size_t harmonic = 0; harmonic < left.harmonics.size(); ++harmonic)
				{
					// -f(1-phase) has coefficients -conj(c[n]), including DC.
					const auto error = std::abs(right.harmonics[harmonic] + std::conj(left.harmonics[harmonic]));
					worstCoefficient = std::max(worstCoefficient, error);
					REQUIRE(error < 0.00015);
				}
				for (const auto phase : { 0.031, 0.153, 0.317, 0.547, 0.793 })
				{
					const auto direct = full.wave(phase, anchor, 100.0 - width, frequency,
						Table::PhaseInterpolation::cubic, Table::WidthInterpolation::cubic);
					const auto reflected = mirrored.wave(phase, anchor, 100.0 - width, frequency,
						Table::PhaseInterpolation::cubic, Table::WidthInterpolation::cubic);
					worstTable = std::max(worstTable, std::abs(direct - reflected));
					REQUIRE(std::abs(direct - reflected) < 2.0e-5);
					double oracle = right.harmonics[0].real();
					for (std::size_t harmonic = 1; harmonic < right.harmonics.size(); ++harmonic)
						oracle += 2.0 * (right.harmonics[harmonic] * std::polar(1.0,
							2.0 * std::numbers::pi * static_cast<double>(harmonic) * phase)).real();
					worstFullOracle = std::max(worstFullOracle, std::abs(direct - oracle));
					worstMirroredOracle = std::max(worstMirroredOracle, std::abs(reflected - oracle));
					// Virtual upper frames preserve the full Width stencil at 50%.
					REQUIRE(std::isfinite(reflected));
					if (std::abs(phase - (100.0 - width) / 100.0) > 0.005)
					{
						const auto ideal = vekt::audio_lab::monoWidthIdealAnchor(anchor, phase, 100.0 - width)
							+ vekt::audio_lab::monoWidthIdealAnchor(anchor, 1.0 - phase, width);
						worstIdeal = std::max(worstIdeal, std::abs(ideal));
						REQUIRE(std::abs(ideal) < 1.0e-10);
						const auto centered = vekt::audio_lab::monoWidthIdealWave(phase, anchor, 100.0 - width, true)
							+ vekt::audio_lab::monoWidthIdealWave(1.0 - phase, anchor, width, true);
						REQUIRE(std::abs(centered) < 1.0e-10);
					}
				}
			}
	std::cout << "mirror worst_coefficient=" << worstCoefficient << " worst_table=" << worstTable
		<< " worst_full_vs_oracle=" << worstFullOracle << " worst_mirrored_vs_oracle=" << worstMirroredOracle
		<< " worst_ideal_away_edges=" << worstIdeal << " full_bytes=" << full.bytes()
		<< " half_bytes=" << mirrored.bytes() << '\n';
}

TEST_CASE("Mono Width adaptive knot coefficient and playback diagnostic",
	"[mono][oscillator][width][comparison][slow]")
{
	using vekt::audio_lab::MonoWidthAdaptiveKnots;
	using Table = vekt::audio_lab::MonoWidthTablePrototype;
	std::vector<double> training, heldOut;
	for (int width = 5; width <= 95; ++width) training.push_back(static_cast<double>(width));
	for (int width = 5; width < 95; ++width) heldOut.push_back(width + 0.5);
	heldOut.insert(heldOut.end(), { 5.25, 7.25, 10.25, 15.25, 20.25, 49.9, 50.1, 79.75, 89.75, 92.75, 94.75 });
	MonoWidthAdaptiveKnots oracle(training);
	const std::vector<double> seed { 5.0, 16.0, 27.0, 38.0, 50.0, 62.0, 73.0, 84.0, 95.0 };
	const auto adaptive17 = oracle.grow(seed, 17);
	const auto adaptive33 = oracle.grow(adaptive17, 33);
	const auto adaptive65 = oracle.grow(adaptive33, 65);
	for (const auto count : { 17, 33, 65 })
	{
		const auto& adaptive = count == 17 ? adaptive17 : count == 33 ? adaptive33 : adaptive65;
		const auto uniform = Table::widths(count, Table::WidthSpacing::uniform);
		const auto adaptiveScore = oracle.evaluate(adaptive, heldOut);
		const auto uniformScore = oracle.evaluate(uniform, heldOut);
		CAPTURE(count);
		REQUIRE(adaptive.size() == static_cast<std::size_t>(count));
		REQUIRE(std::is_sorted(adaptive.begin(), adaptive.end()));
		REQUIRE(adaptive.front() == Catch::Approx(5.0));
		REQUIRE(adaptive.back() == Catch::Approx(95.0));
		REQUIRE(std::isfinite(adaptiveScore.ratio));
		std::cout << "adaptive-width count=" << count << " heldout=" << heldOut.size()
			<< " uniform_dBc=" << db(uniformScore.ratio, 1.0) << " uniform_at=" << uniformScore.bin
			<< '/' << uniformScore.width << '/' << uniformScore.morph
			<< " adaptive_dBc=" << db(adaptiveScore.ratio, 1.0) << " adaptive_at="
			<< adaptiveScore.bin << '/' << adaptiveScore.width << '/' << adaptiveScore.morph << " knots=";
		for (const auto width : adaptive) std::cout << width << ',';
		std::cout << '\n';
	}
	// Build the actual fixed-resolution playback bank as a separate end-to-end
	// check: spectral training never uses interpolated phase tables or decimation.
	const auto low = 37.0 * hostRate / fftSize, high = 151.0 * hostRate / fftSize;
	const vekt::audio_lab::MonoWidthTablePrototype bank(adaptive65, 11, hostRate * 4.0, hostRate, { low, high });
	const vekt::audio_lab::MonoWidthTablePrototype uniformBank(Table::widths(65, Table::WidthSpacing::uniform),
		11, hostRate * 4.0, hostRate, { low, high });
	std::array<double, 2> worstPlayback { -300.0, -300.0 }, worstAlias { -300.0, -300.0 };
	std::array<double, 2> maxDc {}, maxRms {};
	std::array<double, 2> worstWidth {};
	for (const auto heldWidth : heldOut)
		for (const auto bin : { 37, 151 })
			for (const auto morph : { 0.0f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f })
			{
				const auto width = static_cast<float>(heldWidth);
				const auto reference = vekt::audio_lab::renderMonoWidthReference(morph, width,
					static_cast<double>(bin) * hostRate / fftSize, hostRate, 0);
				for (const auto factor : { 1, 4 })
				{
					const auto ideal = render(Renderer::reference, factor, bin, morph, width, reference.harmonics);
					for (std::size_t variant = 0; variant < 2; ++variant)
					{
						const auto& candidate = variant == 0 ? uniformBank : bank;
						const auto actual = render(Renderer::table, factor, bin, morph, width, {}, nullptr, &candidate,
							Table::PhaseInterpolation::cubic, Table::WidthInterpolation::cubic);
						const auto result = score(actual, ideal, bin);
						const auto harmonic = db(result.harmonicError, result.signalPower);
						if (harmonic > worstPlayback[variant])
						{
							worstPlayback[variant] = harmonic;
							worstWidth[variant] = width;
						}
						worstAlias[variant] = std::max(worstAlias[variant], db(result.aliasPower, result.signalPower));
						maxDc[variant] = std::max(maxDc[variant], std::abs(result.dcError));
						maxRms[variant] = std::max(maxRms[variant], std::abs(result.rmsError));
						CAPTURE(bin, width, morph, factor, variant);
						REQUIRE(std::isfinite(result.timeError));
					}
				}
			}
	for (std::size_t variant = 0; variant < 2; ++variant)
		std::cout << "adaptive-heldout-playback bank=" << (variant == 0 ? "uniform" : "adaptive")
			<< " widths=" << heldOut.size() << " factors=1,4 worst_harmonic=" << worstPlayback[variant]
			<< " worst_width=" << worstWidth[variant]
			<< " worst_alias=" << worstAlias[variant] << " max_dc=" << maxDc[variant]
			<< " max_rms=" << maxRms[variant] << '\n';
	REQUIRE(worstPlayback[0] < worstPlayback[1]);
	REQUIRE(worstPlayback[0] < -30.0);
	for (const auto [bin, width, morph] : { std::tuple { 37, 90.5f, 3.0f },
		std::tuple { 151, 49.9f, 2.5f }, std::tuple { 37, 10.5f, 3.0f } })
	{
		const auto reference = vekt::audio_lab::renderMonoWidthReference(morph, width,
			static_cast<double>(bin) * hostRate / fftSize, hostRate, 0);
		const auto ideal = render(Renderer::reference, 4, bin, morph, width, reference.harmonics);
		const auto actual = render(Renderer::table, 4, bin, morph, width, {}, nullptr, &bank,
			vekt::audio_lab::MonoWidthTablePrototype::PhaseInterpolation::cubic,
			vekt::audio_lab::MonoWidthTablePrototype::WidthInterpolation::cubic);
		const auto result = score(actual, ideal, bin);
		CAPTURE(bin, width, morph);
		REQUIRE(std::isfinite(result.timeError));
		std::cout << "adaptive-playback bin=" << bin << " width=" << width << " morph=" << morph
			<< " alias_dBc=" << db(result.aliasPower, result.signalPower)
			<< " harmonic_dBc=" << db(result.harmonicError, result.signalPower)
			<< " dc_error=" << result.dcError << " rms_error=" << result.rmsError << '\n';
	}
}

TEST_CASE("Mono Width table worst square partials and neutral Width against Fourier oracle",
	"[mono][oscillator][width][comparison][slow]")
{
	using Table = vekt::audio_lab::MonoWidthTablePrototype;
	const auto low = 37.0 * hostRate / fftSize, high = 151.0 * hostRate / fftSize;
	const Table bank(Table::widths(65, Table::WidthSpacing::uniform), 11,
		hostRate * 4.0, hostRate, { low, high });
	for (const auto [bin, width, morph, factor] : { std::tuple { 37, 90.0f, 3.0f, 4 },
		std::tuple { 151, 50.0f, 0.0f, 1 }, std::tuple { 151, 50.0f, 1.0f, 1 },
		std::tuple { 151, 50.0f, 2.0f, 1 }, std::tuple { 151, 50.0f, 3.0f, 1 },
		std::tuple { 151, 50.0f, 2.0f, 4 }, std::tuple { 151, 50.0f, 3.0f, 4 } })
	{
		const auto reference = vekt::audio_lab::renderMonoWidthReference(morph, width,
			static_cast<double>(bin) * hostRate / fftSize, hostRate, 0);
		const auto ideal = render(Renderer::reference, factor, bin, morph, width, reference.harmonics);
		const auto table = render(Renderer::table, factor, bin, morph, width, {}, nullptr, &bank,
			Table::PhaseInterpolation::cubic, Table::WidthInterpolation::cubic);
		const auto tableScore = score(table, ideal, bin);
		CAPTURE(bin, width, morph, factor);
		REQUIRE(std::isfinite(tableScore.timeError));
		std::cout << "oracle-breakdown bin=" << bin << " width=" << width << " morph=" << morph
			<< " factor=" << factor << " table alias/harmonic=" << db(tableScore.aliasPower, tableScore.signalPower)
			<< '/' << db(tableScore.harmonicError, tableScore.signalPower)
			<< " dc=" << tableScore.dcError << " rms=" << tableScore.rmsError << '\n';
		if (width != 90.0f) continue;
		const auto targetBins = spectrum(ideal), actualBins = spectrum(table);
		double totalError {};
		for (int harmonic = 1; harmonic * bin < fftSize / 2; ++harmonic)
		{
			const auto index = static_cast<std::size_t>(harmonic * bin);
			const auto error = 2.0 * std::norm(actualBins[index] - targetBins[index]);
			totalError += error;
			std::cout << "partial H=" << harmonic << " ref_mag=" << 2.0 * std::abs(targetBins[index])
				<< " table_mag=" << 2.0 * std::abs(actualBins[index])
				<< " abs_complex_error=" << 2.0 * std::abs(actualBins[index] - targetBins[index])
				<< " error_power_fraction=" << error / tableScore.harmonicError << '\n';
		}
		REQUIRE(totalError == Catch::Approx(tableScore.harmonicError).epsilon(1.0e-4));
	}
}

TEST_CASE("Mono Width offline fixed 65 frames Width interpolation order comparison",
	"[mono][oscillator][width][comparison][slow]")
{
	using Table = vekt::audio_lab::MonoWidthTablePrototype;
	const auto low = 37.0 * hostRate / fftSize, high = 151.0 * hostRate / fftSize;
	const Table full(Table::widths(65, Table::WidthSpacing::uniform), 11,
		hostRate * 4.0, hostRate, { low, high });
	const auto allFrames = Table::widths(65, Table::WidthSpacing::uniform);
	const Table half(std::vector<double>(allFrames.begin(), allFrames.begin() + 33), 11,
		hostRate * 4.0, hostRate, { low, high }, true);
	for (const auto taps : { Table::WidthInterpolation::cubic, Table::WidthInterpolation::lagrange6,
		Table::WidthInterpolation::lagrange8 })
	{
		double worstHarmonic = -300.0, worstAlias = -300.0, worstPartial = -300.0, maxMirrorDifference {};
		int worstBin {}, worstPartialIndex {};
		float worstWidth {}, worstMorph {};
		for (const auto width : { 5.5f, 7.5f, 10.5f, 15.25f, 20.5f, 29.5f, 49.9f,
			50.0f, 50.1f, 65.5f, 79.5f, 90.0f, 90.5f, 93.5f, 94.5f })
			for (const auto bin : { 37, 151 })
				for (const auto morph : { 0.0f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f })
				{
					const auto frequency = static_cast<double>(bin) * hostRate / fftSize;
					const auto reference = vekt::audio_lab::renderMonoWidthReference(morph, width, frequency, hostRate, 0);
					const auto ideal = render(Renderer::reference, 4, bin, morph, width, reference.harmonics);
					const auto actual = render(Renderer::table, 4, bin, morph, width, {}, nullptr, &full,
						Table::PhaseInterpolation::cubic, taps);
					const auto reflected = render(Renderer::table, 4, bin, morph, width, {}, nullptr, &half,
						Table::PhaseInterpolation::cubic, taps);
					const auto result = score(actual, ideal, bin);
					const auto harmonic = db(result.harmonicError, result.signalPower);
					if (harmonic > worstHarmonic)
					{
						worstHarmonic = harmonic;
						worstBin = bin; worstWidth = width; worstMorph = morph;
					}
					worstAlias = std::max(worstAlias, db(result.aliasPower, result.signalPower));
					const auto targetBins = spectrum(ideal), actualBins = spectrum(actual);
					for (int partial = 1; partial * bin < fftSize / 2; ++partial)
					{
						const auto index = static_cast<std::size_t>(partial * bin);
						const auto partialError = db(2.0 * std::norm(actualBins[index] - targetBins[index]),
							result.signalPower);
						if (partialError > worstPartial) { worstPartial = partialError; worstPartialIndex = partial; }
					}
					for (std::size_t index = 0; index < actual.size(); ++index)
						maxMirrorDifference = std::max(maxMirrorDifference, std::abs(actual[index] - reflected[index]));
					CAPTURE(width, bin, morph, taps);
					REQUIRE(std::isfinite(result.timeError));
				}
		std::cout << "width-order taps=" << (taps == Table::WidthInterpolation::cubic ? 4
			: taps == Table::WidthInterpolation::lagrange6 ? 6 : 8)
			<< " worst_harmonic=" << worstHarmonic << " worst_alias=" << worstAlias
			<< " worst_partial_dBc=" << worstPartial << " worst_partial_H=" << worstPartialIndex
			<< " worst_at=" << worstBin << '/' << worstWidth << '/' << worstMorph
			<< " max_mirror_difference=" << maxMirrorDifference << '\n';
		REQUIRE(maxMirrorDifference < 2.0e-5);
	}
}

TEST_CASE("Mono Width coefficient-only Width density by harmonic band diagnostic",
	"[mono][oscillator][width][comparison][slow]")
{
	using Table = vekt::audio_lab::MonoWidthTablePrototype;
	const std::vector<double> probes { 5.5, 10.5, 20.5, 49.9, 65.5, 90.5, 94.5 };
	vekt::audio_lab::MonoWidthAdaptiveKnots oracle({});
	for (const auto count : { 17, 33, 65 })
	{
		const auto knots = Table::widths(count, Table::WidthSpacing::uniform);
		for (const auto limit : { 6, 27 })
		{
			double worstRatio {};
			int worstAnchor {};
			double worstWidth {};
			for (const auto width : probes)
				for (int anchor = 0; anchor < 4; ++anchor)
				{
					const auto& expected = oracle.at(width)[static_cast<std::size_t>(anchor)];
					const auto actual = vekt::audio_lab::MonoWidthAdaptiveKnots::interpolate(knots, width, anchor, limit, oracle);
					double power {}, error {};
					for (int harmonic = 1; harmonic <= limit; ++harmonic)
					{
						const auto index = static_cast<std::size_t>(harmonic);
						power += 2.0 * std::norm(expected[index]);
						error += 2.0 * std::norm(actual[index] - expected[index]);
					}
					const auto ratio = error / power;
					if (ratio > worstRatio) { worstRatio = ratio; worstAnchor = anchor; worstWidth = width; }
					REQUIRE(std::isfinite(ratio));
				}
			std::cout << "band-width-density frames=" << count << " harmonics=" << limit
				<< " worst_error_dBc=" << db(worstRatio, 1.0) << " worst_width=" << worstWidth
				<< " worst_anchor=" << worstAnchor << '\n';
		}
	}
}

TEST_CASE("Mono Width offline guarded pitch-band crossfades preserve boundary continuity",
	"[mono][oscillator][width][comparison][slow]")
{
	using Table = vekt::audio_lab::MonoWidthTablePrototype;
	constexpr double guard = 0.9, fadeStart = 0.8;
	const std::vector<double> ceilings { 900.0, 1800.0, 3600.0 };
	const auto stored = Table::widths(65, Table::WidthSpacing::uniform);
	const std::vector<double> half(stored.begin(), stored.begin() + 33);
	const Table smooth(half, 11, 4.0 * hostRate, hostRate, ceilings, true, guard, fadeStart);
	const Table switched(half, 11, 4.0 * hostRate, hostRate, ceilings, true, guard);
	for (const auto ceiling : ceilings)
	{
		const auto limit = smooth.harmonicLimit(ceiling, 4.0 * hostRate, hostRate);
		CAPTURE(ceiling, limit);
		REQUIRE(limit * ceiling < guard * hostRate / 2.0);
	}
	for (const auto boundary : { 900.0, 1800.0 })
	{
		const auto start = fadeStart * boundary;
		for (const auto width : { 10.5, 90.5 })
			for (const auto morph : { 1.5, 3.0 })
			{
				double worstEdge {}, worstJump {}, worstRmsStep {}, worstHarmonicStep {};
				const auto phase = 0.137;
				const auto wave = [&](double pitch)
				{
					return smooth.wave(phase, morph, width, pitch, Table::PhaseInterpolation::cubic,
						Table::WidthInterpolation::cubic);
				};
				for (const auto edge : { start, boundary })
				{
					const auto before = wave(std::nextafter(edge, 0.0));
					const auto after = wave(std::nextafter(edge, std::numeric_limits<double>::infinity()));
					worstEdge = std::max(worstEdge, std::abs(after - before));
				}
				const auto left = switched.wave(phase, morph, width, boundary,
					Table::PhaseInterpolation::cubic, Table::WidthInterpolation::cubic);
				const auto right = switched.wave(phase, morph, width, std::nextafter(boundary,
					std::numeric_limits<double>::infinity()), Table::PhaseInterpolation::cubic, Table::WidthInterpolation::cubic);
				worstJump = std::abs(right - left);
				REQUIRE(worstJump > 1.0e-3);
				// Coherent cycle FFT: harmonic amplitudes and RMS on both sides of
				// each edge. This isolates table switching from oscillator phase drift.
				const auto cycle = [&](double pitch)
				{
					std::vector<double> samples(fftSize);
					for (int n = 0; n < fftSize; ++n)
						samples[static_cast<std::size_t>(n)] = smooth.wave(static_cast<double>(n) / fftSize,
							morph, width, pitch, Table::PhaseInterpolation::cubic, Table::WidthInterpolation::cubic);
					return samples;
				};
				for (const auto edge : { start, boundary })
				{
					const auto before = cycle(std::nextafter(edge, 0.0));
					const auto after = cycle(std::nextafter(edge, std::numeric_limits<double>::infinity()));
					const auto a = spectrum(before), b = spectrum(after);
					const auto rms = [](const std::vector<double>& samples)
					{
						double power {};
						for (const auto sample : samples) power += sample * sample / fftSize;
						return std::sqrt(power);
					};
					worstRmsStep = std::max(worstRmsStep, std::abs(rms(before) - rms(after)));
					for (std::size_t h = 1; h < a.size(); ++h)
						worstHarmonicStep = std::max(worstHarmonicStep, std::abs(a[h] - b[h]));
				}
				CAPTURE(boundary, width, morph, worstEdge, worstJump, worstRmsStep, worstHarmonicStep);
				REQUIRE(worstEdge < 1.0e-5);
				REQUIRE(worstRmsStep < 1.0e-5);
				REQUIRE(worstHarmonicStep < 1.0e-5);
				std::cout << "pitch-transition boundary=" << boundary << " width=" << width
					<< " morph=" << morph << " max_edge=" << worstEdge << " hard_jump=" << worstJump
					<< " rms_step=" << worstRmsStep << " harmonic_step=" << worstHarmonicStep << '\n';
			}
	}
}

TEST_CASE("Mono Width offline guarded pitch-band sweep and phase-continuous glide",
	"[mono][oscillator][width][comparison][slow]")
{
	using Table = vekt::audio_lab::MonoWidthTablePrototype;
	const auto widths = Table::widths(65, Table::WidthSpacing::uniform);
	const std::vector<double> half(widths.begin(), widths.begin() + 33);
	const std::vector<double> ceilings { 900.0, 1800.0, 3600.0 };
	const Table faded(half, 11, 4.0 * hostRate, hostRate, ceilings, true, 0.9, 0.8);
	const Table hard(half, 11, 4.0 * hostRate, hostRate, ceilings, true, 0.9);
	for (const auto boundary : { 900.0, 1800.0 })
		for (const auto width : { 10.5, 90.5 })
			for (const auto morph : { 1.5, 3.0 })
			{
				double worstHarmonic = -300.0, worstOutOfTargetHarmonics = -300.0;
				double maxRmsError {}, maxTimeError {}, maxAdjacentHarmonicChange {};
				std::vector<std::complex<double>> previous;
				for (int step = 0; step <= 20; ++step)
				{
					const auto pitch = boundary * (0.78 + 0.011 * step);
					const auto reference = vekt::audio_lab::renderMonoWidthReference(morph, width, pitch, hostRate, 0);
					std::vector<double> actual(fftSize);
					for (int index = 0; index < fftSize; ++index)
						actual[static_cast<std::size_t>(index)] = faded.wave(static_cast<double>(index) / fftSize,
							morph, width, pitch, Table::PhaseInterpolation::cubic, Table::WidthInterpolation::cubic);
					const auto bins = spectrum(actual);
					double power {}, harmonicError {}, nonharmonic {}, timeError {}, actualPower {};
				for (int index = 0; index < fftSize; ++index)
				{
						const auto phase = static_cast<double>(index) / fftSize;
						double ideal = reference.harmonics[0].real();
						const auto unit = std::polar(1.0, 2.0 * std::numbers::pi * phase);
						auto carrier = unit;
						for (std::size_t h = 1; h < reference.harmonics.size(); ++h)
						{
							ideal += 2.0 * (reference.harmonics[h] * carrier).real();
							carrier *= unit;
						}
						const auto delta = actual[static_cast<std::size_t>(index)] - ideal;
						timeError += delta * delta / fftSize;
						power += ideal * ideal / fftSize;
						actualPower += actual[static_cast<std::size_t>(index)] * actual[static_cast<std::size_t>(index)] / fftSize;
					}
					for (std::size_t h = 1; h < bins.size() - 1; ++h)
					{
						const auto expected = h < reference.harmonics.size() ? reference.harmonics[h] : std::complex<double> {};
						harmonicError += 2.0 * std::norm(bins[h] - expected);
						if (h >= reference.harmonics.size()) nonharmonic += 2.0 * std::norm(bins[h]);
						if (!previous.empty() && h < reference.harmonics.size())
							maxAdjacentHarmonicChange = std::max(maxAdjacentHarmonicChange, std::abs(bins[h] - previous[h]));
					}
					previous = bins;
					worstHarmonic = std::max(worstHarmonic, db(harmonicError, power));
					worstOutOfTargetHarmonics = std::max(worstOutOfTargetHarmonics, db(nonharmonic, power));
					maxRmsError = std::max(maxRmsError, std::abs(std::sqrt(actualPower) - std::sqrt(power)));
					maxTimeError = std::max(maxTimeError, std::sqrt(timeError));
				}
				// A half-second pitch glide retains oscillator phase across the bands.
				// Compare consecutive sample steps with the same phase trajectory for
				// both policies; do not interpret the broadband glide FFT as alias power.
				double phase {}, maxFadedStep {}, maxHardStep {}, maxFadedPitchStep {}, maxHardPitchStep {}, differencePower {};
				for (int index = 0; index < hostRate / 2; ++index)
				{
					const auto pitch = boundary * (0.78 + 0.44 * index / (hostRate / 2 - 1.0));
					const auto a = faded.wave(phase, morph, width, pitch,
						Table::PhaseInterpolation::cubic, Table::WidthInterpolation::cubic);
					const auto b = hard.wave(phase, morph, width, pitch,
						Table::PhaseInterpolation::cubic, Table::WidthInterpolation::cubic);
					if (index > 0)
					{
						const auto previousPitch = boundary * (0.78 + 0.44 * (index - 1) / (hostRate / 2 - 1.0));
						const auto previousPhase = phase - previousPitch / hostRate;
						maxFadedPitchStep = std::max(maxFadedPitchStep, std::abs(a - faded.wave(phase, morph,
							width, previousPitch, Table::PhaseInterpolation::cubic, Table::WidthInterpolation::cubic)));
						maxHardPitchStep = std::max(maxHardPitchStep, std::abs(b - hard.wave(phase, morph,
							width, previousPitch, Table::PhaseInterpolation::cubic, Table::WidthInterpolation::cubic)));
						maxFadedStep = std::max(maxFadedStep, std::abs(a - faded.wave(previousPhase, morph,
							width, previousPitch, Table::PhaseInterpolation::cubic, Table::WidthInterpolation::cubic)));
						maxHardStep = std::max(maxHardStep, std::abs(b - hard.wave(previousPhase, morph,
							width, previousPitch, Table::PhaseInterpolation::cubic, Table::WidthInterpolation::cubic)));
						const auto delta = a - b;
						differencePower += delta * delta / (hostRate / 2);
					}
					phase += pitch / hostRate;
					phase -= std::floor(phase);
				}
				CAPTURE(boundary, width, morph);
				REQUIRE(std::isfinite(worstHarmonic));
				REQUIRE(std::isfinite(worstOutOfTargetHarmonics));
				REQUIRE(std::isfinite(maxFadedStep));
				REQUIRE(maxFadedPitchStep < 1.0e-3);
				REQUIRE(maxHardPitchStep > 1.0e-3);
				std::cout << "pitch-sweep boundary=" << boundary << " width=" << width << " morph=" << morph
					<< " worst_harmonic=" << worstHarmonic << " out_of_target_harmonics=" << worstOutOfTargetHarmonics
					<< " max_rms_error=" << maxRmsError << " max_time_error=" << maxTimeError
					<< " max_neighbor_coefficient_change=" << maxAdjacentHarmonicChange
					<< " glide_pitch_step_faded=" << maxFadedPitchStep << " glide_pitch_step_hard=" << maxHardPitchStep
					<< " glide_max_step_faded=" << maxFadedStep << " glide_max_step_hard=" << maxHardStep
					<< " glide_faded_vs_hard_rms=" << std::sqrt(differencePower) << '\n';
			}
}

TEST_CASE("Mono Width guarded band transitions matched host-rate playback",
	"[mono][oscillator][width][comparison][slow]")
{
	using Table = vekt::audio_lab::MonoWidthTablePrototype;
	const auto widths = Table::widths(65, Table::WidthSpacing::uniform);
	const Table faded(std::vector<double>(widths.begin(), widths.begin() + 33), 11,
		4.0 * hostRate, hostRate, { 900.0, 1800.0, 3600.0 }, true, 0.9, 0.8);
	for (const auto boundaryBin : { 38, 77 })
	{
		double worstHarmonic = -300.0, worstNonharmonic = -300.0, maxRmsError {}, maxTimeError {};
		for (const auto bin : { boundaryBin - 2, boundaryBin - 1, boundaryBin, boundaryBin + 1, boundaryBin + 2 })
			for (const auto width : { 10.5f, 90.5f })
				for (const auto morph : { 1.5f, 3.0f })
					for (const auto factor : { 1, 4 })
					{
						const auto pitch = static_cast<double>(bin) * hostRate / fftSize;
						const auto reference = vekt::audio_lab::renderMonoWidthReference(morph, width, pitch, hostRate, 0);
						const auto ideal = render(Renderer::reference, factor, bin, morph, width, reference.harmonics);
						const auto actual = render(Renderer::table, factor, bin, morph, width, {}, nullptr, &faded,
							Table::PhaseInterpolation::cubic, Table::WidthInterpolation::cubic);
						const auto result = score(actual, ideal, bin);
						worstHarmonic = std::max(worstHarmonic, db(result.harmonicError, result.signalPower));
						worstNonharmonic = std::max(worstNonharmonic, db(result.aliasPower, result.signalPower));
						maxRmsError = std::max(maxRmsError, std::abs(result.rmsError));
						maxTimeError = std::max(maxTimeError, std::sqrt(result.timeError));
						CAPTURE(bin, width, morph, factor);
						REQUIRE(std::isfinite(result.timeError));
					}
		std::cout << "pitch-playback boundary_bin=" << boundaryBin << " worst_harmonic=" << worstHarmonic
			<< " worst_nonharmonic=" << worstNonharmonic << " max_rms_error=" << maxRmsError
			<< " max_time_error=" << maxTimeError << '\n';
	}
}

TEST_CASE("Mono Width per-harmonic guard policy and one-harmonic bank",
	"[mono][oscillator][width][comparison][slow]")
{
	using Table = vekt::audio_lab::MonoWidthTablePrototype;
	constexpr double guard = 0.9, start = 0.8;
	const auto nyquist = hostRate / 2.0;
	const auto widths = Table::widths(65, Table::WidthSpacing::uniform);
	const std::vector<double> half(widths.begin(), widths.begin() + 33);
	const auto schedule = [&](int bandsPerOctave)
	{
		// DC remains after the final fundamental fade (limit 0, ceiling Nyquist).
		auto limits = vekt::audio_lab::monoWidthLevelLimits(32, bandsPerOctave);
		std::vector<double> ceilings;
		for (const auto limit : limits) ceilings.push_back(limit > 0 ? guard * nyquist / limit : nyquist);
		return std::pair { std::move(ceilings), std::move(limits) };
	};
	const auto [oneCeilings, oneLimits] = schedule(1000); // each step removes exactly one partial
	REQUIRE(oneLimits.size() == 33);
	REQUIRE(oneLimits.front() == 32);
	REQUIRE(oneLimits.back() == 0);
	for (std::size_t band = 0; band + 1 < oneLimits.size(); ++band)
	{
		CAPTURE(band);
		REQUIRE(oneLimits[band] == oneLimits[band + 1] + 1);
		REQUIRE(oneLimits[band] * oneCeilings[band] == Catch::Approx(guard * nyquist));
	}
	const Table fine(half, 11, hostRate * 4.0, hostRate, oneCeilings, true,
		guard, start / guard, oneLimits, true);
	// Check the independent envelope at and on either side of a few
	// transition points, including the last transition to DC-only output.
	for (const auto h : { 1, 2, 11, 23 })
		for (const auto normalized : { 0.79, 0.8, 0.85, 0.9, 0.91 })
		{
			const auto pitch = normalized * nyquist / h;
			const auto gain = vekt::audio_lab::monoWidthGuardGain(h, pitch, hostRate);
			CAPTURE(h, normalized);
			REQUIRE(gain >= 0.0);
			REQUIRE(gain <= 1.0);
			if (std::abs(normalized - 0.85) < 1.0e-12) REQUIRE(gain == Catch::Approx(0.5).margin(1.0e-12));
			if (normalized >= 0.9) REQUIRE(gain < 1.0e-12);
		}
	for (const auto width : { 10.5f, 90.5f })
		for (const auto morph : { 1.5f, 3.0f })
		{
			for (const auto h : { 1, 2, 11, 23 })
				for (const auto normalized : { 0.8, 0.85, 0.9 })
				{
					const auto pitch = normalized * nyquist / h;
					if (pitch < oneCeilings.front()) continue; // finite H32 first level
					const auto reference = vekt::audio_lab::renderMonoWidthReference(morph, width, pitch, hostRate, 0);
					const auto guarded = vekt::audio_lab::monoWidthGuardedHarmonics(reference.harmonics,
						pitch, hostRate);
					const auto phase = 0.137;
					double expected = guarded[0].real();
					for (std::size_t partial = 1; partial < guarded.size(); ++partial)
						expected += 2.0 * (guarded[partial] * std::polar(1.0,
							2.0 * std::numbers::pi * static_cast<double>(partial) * phase)).real();
					const auto actual = fine.wave(phase, morph, width, pitch,
						Table::PhaseInterpolation::cubic, Table::WidthInterpolation::cubic);
					CAPTURE(width, morph, h, normalized);
					REQUIRE(std::abs(actual - expected) < 0.05);
				}
			const auto top = vekt::audio_lab::renderMonoWidthReference(morph, width, 23000.0, hostRate, 0);
			for (const auto phase : { 0.137, 0.5, 0.875 })
			{
				CAPTURE(width, morph, phase);
				REQUIRE(fine.wave(phase, morph, width, 23000.0, Table::PhaseInterpolation::cubic,
					Table::WidthInterpolation::cubic) == Catch::Approx(top.harmonics[0].real()).margin(1.0e-4));
			}
		}
	// H22 is already entering the taper while H23 is half faded. The bank
	// must keep both independent gains, not switch a single active table pair.
	{
		const auto pitch = 0.85 * nyquist / 23.0;
		const auto reference = vekt::audio_lab::renderMonoWidthReference(2.0, 10.5, pitch, hostRate, 0);
		const auto guarded = vekt::audio_lab::monoWidthGuardedHarmonics(reference.harmonics,
			pitch, hostRate);
		REQUIRE(vekt::audio_lab::monoWidthGuardGain(23, pitch, hostRate) == Catch::Approx(0.5));
		REQUIRE(vekt::audio_lab::monoWidthGuardGain(22, pitch, hostRate) < 1.0);
		REQUIRE(vekt::audio_lab::monoWidthGuardGain(22, pitch, hostRate) > 0.0);
		std::vector<double> cycle(fftSize);
		for (int index = 0; index < fftSize; ++index)
			cycle[static_cast<std::size_t>(index)] = fine.wave(static_cast<double>(index) / fftSize,
				2.0, 10.5, pitch, Table::PhaseInterpolation::cubic, Table::WidthInterpolation::cubic);
		const auto actual = spectrum(cycle);
		for (const auto harmonic : { 22, 23 })
		{
			CAPTURE(harmonic);
			REQUIRE(std::abs(actual[static_cast<std::size_t>(harmonic)]
				- guarded[static_cast<std::size_t>(harmonic)]) < 5.0e-4);
		}
	}
	std::cout << "harmonic-ladder levels=" << oneLimits.size() << " bytes=" << fine.bytes()
		<< " minimum_test_pitch=" << oneCeilings.front()
		<< " fundamental_fades_to_dc_at=" << guard * nyquist << '\n';
	for (const auto bandsPerOctave : { 1, 2, 4, 1000 })
	{
		const auto [ceilings, limits] = schedule(bandsPerOctave);
		const Table bank(half, 11, hostRate * 4.0, hostRate, ceilings, true,
			guard, start / guard, limits, true);
		double worstHarmonic = -300.0, worstNonharmonic = -300.0, maxRmsError {}, maxTimeError {};
		int worstBin {}, worstFactor {};
		float worstWidth {}, worstMorph {};
		for (const auto bin : { 31, 37, 38, 39, 75, 76, 77, 78, 151, 184, 190, 230 })
			for (const auto width : { 10.5f, 90.5f })
				for (const auto morph : { 1.5f, 3.0f })
					for (const auto factor : { 1, 4 })
					{
						const auto pitch = static_cast<double>(bin) * hostRate / fftSize;
						const auto reference = vekt::audio_lab::renderMonoWidthReference(morph, width, pitch, hostRate, 0);
						const auto guarded = vekt::audio_lab::monoWidthGuardedHarmonics(reference.harmonics,
							pitch, hostRate);
						const auto ideal = render(Renderer::reference, factor, bin, morph, width, guarded);
						const auto actual = render(Renderer::table, factor, bin, morph, width, {}, nullptr, &bank,
							Table::PhaseInterpolation::cubic, Table::WidthInterpolation::cubic);
						const auto result = score(actual, ideal, bin);
						const auto harmonicError = db(result.harmonicError, result.signalPower);
						if (harmonicError > worstHarmonic)
						{
							worstHarmonic = harmonicError;
							worstBin = bin; worstWidth = width; worstMorph = morph; worstFactor = factor;
						}
						worstNonharmonic = std::max(worstNonharmonic, db(result.aliasPower, result.signalPower));
						maxRmsError = std::max(maxRmsError, std::abs(result.rmsError));
						maxTimeError = std::max(maxTimeError, std::sqrt(result.timeError));
						CAPTURE(bandsPerOctave, bin, width, morph, factor);
						REQUIRE(std::isfinite(result.timeError));
					}
		std::cout << "harmonic-density bands_per_octave=" << bandsPerOctave << " levels=" << limits.size()
			<< " bytes=" << bank.bytes() << " worst_harmonic=" << worstHarmonic
			<< " worst_at=" << worstBin << '/' << worstWidth << '/' << worstMorph << '/' << worstFactor
			<< " worst_nonharmonic=" << worstNonharmonic << " max_rms_error=" << maxRmsError
			<< " max_time_error=" << maxTimeError << '\n';
	}
}
TEST_CASE("Mono Width grouped level model matches the table bank spectrum and playback",
	"[mono][oscillator][width][comparison][slow]")
{
	using Table = vekt::audio_lab::MonoWidthTablePrototype;
	constexpr double guard = 0.9, start = 0.8;
	const auto nyquist = hostRate / 2.0;
	const auto widths = Table::widths(65, Table::WidthSpacing::uniform);
	const auto limits = vekt::audio_lab::monoWidthLevelLimits(32, 4.0);
	std::vector<double> ceilings;
	for (const auto limit : limits) ceilings.push_back(limit > 0 ? guard * nyquist / limit : nyquist);
	const Table bank(std::vector<double>(widths.begin(), widths.begin() + 33), 11, hostRate * 4.0, hostRate,
		ceilings, true, guard, start / guard, limits, true);
	double worstCoefficient {};
	for (const auto bin : { 31, 37, 39, 76, 151, 190, 230 })
		for (const auto width : { 10.5f, 90.5f })
			for (const auto morph : { 1.5f, 3.0f })
			{
				const auto pitch = static_cast<double>(bin) * hostRate / fftSize;
				const auto reference = vekt::audio_lab::renderMonoWidthReference(morph, width, pitch, hostRate, 0);
				std::vector<double> cycle(fftSize);
				for (int index = 0; index < fftSize; ++index)
					cycle[static_cast<std::size_t>(index)] = bank.wave(static_cast<double>(index) / fftSize, morph, width,
						pitch, Table::PhaseInterpolation::cubic, Table::WidthInterpolation::cubic);
				const auto actual = spectrum(cycle);
				for (std::size_t harmonic = 1; harmonic < reference.harmonics.size(); ++harmonic)
				{
					const auto gain = vekt::audio_lab::monoWidthGroupedGain(static_cast<int>(harmonic), limits,
						pitch, hostRate);
					worstCoefficient = std::max(worstCoefficient,
						std::abs(actual[harmonic] - gain * reference.harmonics[harmonic]));
				}
			}
	// Residual is Width/phase lookup error of the 33-frame mirrored bank, not grouping.
	std::cout << "level-model worst_coefficient_error=" << worstCoefficient << '\n';
	REQUIRE(worstCoefficient < 2.5e-3);
	// Matched playback against the guarded oracle. At 1x the downsampler is
	// bypassed, so the coefficient model should predict the measured error
	// wherever it lies well above the Width/phase lookup floor.
	for (const auto bin : { 37, 39, 151, 230 })
		for (const auto factor : { 1, 2, 4, 8 })
		{
			const auto pitch = static_cast<double>(bin) * hostRate / fftSize;
			const auto reference = vekt::audio_lab::renderMonoWidthReference(3.0, 10.5, pitch, hostRate, 0);
			const auto guarded = vekt::audio_lab::monoWidthGuardedHarmonics(reference.harmonics, pitch, hostRate);
			const auto model = vekt::audio_lab::monoWidthLevelError(reference.harmonics, limits, pitch, hostRate);
			const auto predicted = db(model.errorPower, model.signalPower);
			const auto ideal = render(Renderer::reference, factor, bin, 3.0f, 10.5f, guarded);
			const auto actual = render(Renderer::table, factor, bin, 3.0f, 10.5f, {}, nullptr, &bank,
				Table::PhaseInterpolation::cubic, Table::WidthInterpolation::cubic);
			const auto result = score(actual, ideal, bin);
			const auto measured = db(result.harmonicError, result.signalPower);
			std::cout << "level-model bin=" << bin << " factor=" << factor << " predicted=" << predicted
				<< " measured=" << measured << '\n';
			CAPTURE(bin, factor, predicted, measured);
			if (factor == 1 && predicted > -32.0) REQUIRE(std::abs(measured - predicted) < 1.5);
		}
}

TEST_CASE("Mono Width grouped pitch-level full-range static sweep",
	"[mono][oscillator][width][comparison][slow]")
{
	// A 2048-sample table holds at most H1023, so the bank's first level is
	// H1023 and pitches below 0.9 * Nyquist / 1023 are reported separately.
	constexpr int topHarmonic = 1023;
	const std::array<double, 11> widths { 5.0, 7.5, 10.0, 20.0, 33.3, 50.0, 66.7, 80.0, 90.0, 92.5, 95.0 };
	const std::array<double, 7> morphs { 0.0, 0.5, 1.0, 1.5, 2.0, 2.5, 3.0 };
	std::vector<std::vector<std::vector<std::complex<double>>>> anchors; // [width][anchor][harmonic]
	for (const auto width : widths)
	{
		auto& perAnchor = anchors.emplace_back();
		for (int anchor = 0; anchor < 4; ++anchor)
		{
			// The shipped Width model: per-anchor depths, aligned, two-tooth saw.
			auto harmonics = vekt::audio_lab::monoWidthAnchorCoefficients(anchor, width);
			harmonics.resize(topHarmonic + 1);
			perAnchor.push_back(std::move(harmonics));
		}
	}
	// One-harmonic levels reproduce the guarded target exactly in coefficient space.
	{
		const auto ladder = vekt::audio_lab::monoWidthLevelLimits(topHarmonic, 1.0e6);
		REQUIRE(ladder.size() == topHarmonic + 1);
		const auto coefficients = vekt::audio_lab::monoWidthBlendCoefficients(anchors[0], 3.0);
		for (const auto pitch : { 30.0, 440.0, 1234.5, 9000.0 })
			REQUIRE(vekt::audio_lab::monoWidthLevelError(coefficients, ladder, pitch, 48'000.0).errorPower < 1.0e-30);
	}
	for (const auto rate : { 44'100.0, 48'000.0, 96'000.0 })
	{
		const auto nyquist = rate / 2.0;
		const auto lowest = 0.9 * nyquist / topHarmonic;
		for (const auto bandsPerOctave : { 1.0, 2.0, 3.0, 4.0, 6.0, 8.0 })
		{
			const auto limits = vekt::audio_lab::monoWidthLevelLimits(topHarmonic, bandsPerOctave);
			// Log grid plus, for every level top, its fade start/middle/end and
			// the point just before the top partial disappears.
			std::vector<double> pitches;
			for (auto pitch = lowest; pitch < 0.9 * nyquist; pitch *= std::exp2(1.0 / 24.0)) pitches.push_back(pitch);
			for (const auto limit : limits)
				if (limit > 0)
					for (const auto ratio : { 0.8, 0.85, 0.899999 })
						if (ratio * nyquist / limit >= lowest) pitches.push_back(ratio * nyquist / limit);
			std::array<double, 12> octaveWorst;
			octaveWorst.fill(-300.0);
			double worst = -300.0, worstPitch {}, worstWidth {}, worstMorph {}, lowestDarkened = 1.0;
			for (const auto pitch : pitches)
				for (std::size_t widthIndex = 0; widthIndex < widths.size(); ++widthIndex)
					for (const auto morph : morphs)
					{
						const auto coefficients = vekt::audio_lab::monoWidthBlendCoefficients(anchors[widthIndex], morph);
						const auto error = vekt::audio_lab::monoWidthLevelError(coefficients, limits, pitch, rate);
						const auto dbc = db(error.errorPower, error.signalPower);
						REQUIRE(std::isfinite(dbc));
						const auto octave = std::clamp(static_cast<int>(std::floor(std::log2(pitch / 20.0))), 0, 11);
						octaveWorst[static_cast<std::size_t>(octave)] = std::max(octaveWorst[static_cast<std::size_t>(octave)], dbc);
						if (dbc > worst)
						{
							worst = dbc; worstPitch = pitch; worstWidth = widths[widthIndex]; worstMorph = morph;
						}
						if (error.lowestDarkenedRatio > 0.0) lowestDarkened = std::min(lowestDarkened, error.lowestDarkenedRatio);
					}
			std::cout << "level-sweep rate=" << rate << " bands_per_octave=" << bandsPerOctave << " levels=" << limits.size()
				<< " lowest_pitch=" << lowest << " worst=" << worst << " worst_at=" << worstPitch << '/' << worstWidth
				<< '/' << worstMorph << " lowest_darkened_hz=" << lowestDarkened * nyquist << " octave_worst(20Hz..)=";
			for (const auto value : octaveWorst) std::cout << (value < -299.0 ? 0.0 : value) << ',';
			std::cout << '\n';
		}
	}
}

TEST_CASE("Mono additive lab oscillator matches the shipped Width reference and level policies",
	"[mono][oscillator][width][comparison]")
{
	// Analytic coefficients versus the independent FFT reference.
	std::vector<std::complex<double>> analytic(200);
	for (int anchor = 0; anchor < 4; ++anchor)
		for (const auto width : { 5.0, 12.5, 37.0, 50.0, 63.7, 88.0, 95.0 })
		{
			CAPTURE(anchor, width);
			const auto reference = vekt::audio_lab::monoWidthAnchorCoefficients(anchor, width);
			vekt::audio_lab::monoWidthAnalyticAnchorCoefficients(anchor, width, analytic.size(), analytic.data());
			for (std::size_t harmonic = 0; harmonic < analytic.size(); ++harmonic)
			{
				CAPTURE(harmonic);
				REQUIRE(std::abs(analytic[harmonic] - reference[harmonic]) < 1.0e-4);
			}
		}
	// Samples equal guarded synthesis from the same coefficients, per policy.
	using Oscillator = vekt::audio_lab::MonoAdditiveOscillator;
	Oscillator oscillator;
	oscillator.setHostRate(hostRate);
	const auto fourBands = vekt::audio_lab::monoWidthLevelLimits(Oscillator::topHarmonic, 4.0);
	const auto twoBands = vekt::audio_lab::monoWidthLevelLimits(Oscillator::topHarmonic, 2.0);
	for (const auto policy : { Oscillator::Policy::perHarmonic, Oscillator::Policy::fourBandsPerOctave,
		Oscillator::Policy::twoBandsPerOctave })
	{
		oscillator.setPolicy(policy);
		for (const auto pitch : { 55.0f, 440.0f, 2703.0f })
			for (const auto morph : { 0.0f, 1.5f, 2.0f, 2.5f, 3.0f })
			{
				const auto width = 83.0f;
				const auto coefficients = vekt::audio_lab::monoWidthShippedCoefficients(morph, width);
				for (const auto phase : { 0.0f, 0.3f, 0.71f })
				{
					double expected = coefficients[0].real();
					for (int harmonic = 1; harmonic <= Oscillator::topHarmonic; ++harmonic)
					{
						const auto gain = policy == Oscillator::Policy::perHarmonic
							? vekt::audio_lab::monoWidthGuardGain(harmonic, pitch, hostRate)
							: vekt::audio_lab::monoWidthGroupedGain(harmonic,
								policy == Oscillator::Policy::fourBandsPerOctave ? fourBands : twoBands, pitch, hostRate);
						expected += 2.0 * gain * (coefficients[static_cast<std::size_t>(harmonic)]
							* std::polar(1.0, 2.0 * std::numbers::pi * harmonic * static_cast<double>(phase))).real();
					}
					CAPTURE(static_cast<int>(policy), pitch, morph, phase);
					// Float lanes for unfaded harmonics.
					REQUIRE(oscillator.sample(phase, pitch, morph, width) == Catch::Approx(expected).margin(1.0e-3));
				}
			}
	}
}

TEST_CASE("Mono production Width oscillator matches the exact 4 bands/octave additive renderer",
	"[mono][oscillator][width][comparison]")
{
	using Policy = vekt::audio_lab::MonoAdditiveOscillator::Policy;
	vekt::audio_lab::MonoAdditiveOscillator additive;
	additive.setPolicy(Policy::fourBandsPerOctave);
	vekt::mono::WidthOscillatorState state;
	for (const auto rate : { 44'100.0, 96'000.0 })
	{
		additive.setHostRate(rate);
		double worst {};
		for (const auto pitch : { 25.0f, 55.0f, 440.0f, 2483.7f, 7000.0f, 21000.0f })
			for (const auto width : { 5.0f, 27.3f, 50.0f, 83.0f, 95.0f })
				for (const auto morph : { 0.0f, 0.4f, 1.0f, 1.7f, 2.0f, 2.5f, 3.0f, 3.3f, 3.8f })
					for (const auto phase : { 0.0f, 0.137f, 0.5f, 0.93f })
					{
						CAPTURE(rate, pitch, width, morph, phase);
						const auto expected = additive.sample(phase, pitch, morph, width);
						const auto actual = vekt::mono::renderWidthOscillator(state, phase, pitch, rate, morph, width, false);
						worst = std::max(worst, static_cast<double>(std::abs(actual - expected)));
						REQUIRE(actual == Catch::Approx(expected).margin(5.0e-4));
						// Zero-centered DC removes exactly the frozen-Width mean.
						const auto centered = vekt::mono::renderWidthOscillator(state, phase, pitch, rate, morph, width, true);
						const auto dc = [&](int anchor)
						{
							std::complex<double> c0[1] {};
							vekt::mono::widthHarmonics(anchor, width, 0, 0, c0);
							return c0[0].real();
						};
						const auto blend = vekt::audio_lab::monoWidthMorphBlend(morph);
						const auto mean = dc(blend.from) + blend.weight * (dc(blend.to) - dc(blend.from));
						REQUIRE(centered == Catch::Approx(actual - mean).margin(1.0e-5));
					}
		std::cout << "production-vs-additive rate=" << rate << " worst_abs_error=" << worst
			<< " bytes=" << vekt::mono::WidthWavetable::instance().bytes() << '\n';
	}
}
