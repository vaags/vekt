#include <vekt/audio_analysis/Measurements.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <numbers>
#include <vector>

TEST_CASE("Audio analysis sample statistics are typed and deterministic", "[audio-analysis]")
{
	constexpr std::array samples { -1.0f, 0.0f, 1.0f };
	const auto measurement = vekt::audio_analysis::measureSamples<float>(samples);
	REQUIRE(measurement.samples == samples.size());
	REQUIRE(measurement.rms == Catch::Approx(std::sqrt(2.0 / 3.0)));
	REQUIRE(measurement.peak == Catch::Approx(1.0));
	REQUIRE(measurement.dc == Catch::Approx(0.0));
	REQUIRE(measurement.differenceRms == Catch::Approx(1.0));
	REQUIRE(measurement.crestFactor == Catch::Approx(1.0 / measurement.rms));
}

TEST_CASE("Audio analysis projects sinusoidal magnitude and phase", "[audio-analysis]")
{
	constexpr double sampleRate = 48'000.0;
	constexpr double frequency = 1'000.0;
	constexpr std::size_t samples = 4'800;
	vekt::audio_analysis::SinusoidalProjector projector(sampleRate, frequency);
	for (std::size_t sample = 0; sample < samples; ++sample)
	{
		const auto phase = 2.0 * std::numbers::pi * frequency * static_cast<double>(sample) / sampleRate;
		projector.add(0.25 * std::sin(phase));
	}
	REQUIRE(projector.peakAmplitude() == Catch::Approx(0.25).margin(1.0e-12));
	REQUIRE(vekt::audio_analysis::wrapPhase(projector.result().phaseRadians() + std::numbers::pi / 2.0)
		== Catch::Approx(0.0).margin(1.0e-12));
}

TEST_CASE("Audio analysis measures ringdown extinction and frequency", "[audio-analysis]")
{
	constexpr double sampleRate = 48'000.0;
	std::vector<float> samples(static_cast<std::size_t>(sampleRate));
	for (std::size_t sample = 0; sample < samples.size(); ++sample)
	{
		const auto time = static_cast<double>(sample) / sampleRate;
		samples[sample] = static_cast<float>(std::sin(2.0 * std::numbers::pi * 1'000.0 * time)
			* std::exp(-20.0 * time));
	}
	const auto measurement = vekt::audio_analysis::measureRingdown<float>(samples, sampleRate);
	REQUIRE(measurement.peak > 0.5);
	REQUIRE(measurement.extinguished);
	REQUIRE(measurement.extinctionSample < samples.size());
	REQUIRE(measurement.estimatedFrequencyHz == Catch::Approx(1'000.0).margin(2.0));
}

TEST_CASE("Audio analysis compares sample sequences exactly", "[audio-analysis]")
{
	constexpr std::array first { 0.0f, -0.0f, 1.0f };
	constexpr std::array same { 0.0f, -0.0f, 1.0f };
	constexpr std::array signedZeroChanged { 0.0f, 0.0f, 1.0f };
	REQUIRE(vekt::audio_analysis::samplesExactlyEqual<float>(first, same));
	REQUIRE_FALSE(vekt::audio_analysis::samplesExactlyEqual<float>(first, signedZeroChanged));
	REQUIRE(vekt::audio_analysis::gainToDecibels(1.0) == Catch::Approx(0.0));
	REQUIRE(vekt::audio_analysis::levelMatchScale(0.5, 0.25) == Catch::Approx(2.0));
}

TEST_CASE("Audio analysis measures transfer gain phase and harmonics", "[audio-analysis]")
{
	constexpr double sampleRate = 48'000.0;
	constexpr double frequency = 1'000.0;
	constexpr std::size_t sampleCount = 4'800;
	std::vector<double> input(sampleCount), output(sampleCount);
	for (std::size_t sample = 0; sample < sampleCount; ++sample)
	{
		const auto phase = 2.0 * std::numbers::pi * frequency * static_cast<double>(sample) / sampleRate;
		input[sample] = std::sin(phase);
		output[sample] = 0.5 * std::sin(phase - 0.25) + 0.1 * std::sin(2.0 * phase);
	}
	const auto transfer = vekt::audio_analysis::measureTransfer<double, double>(
		input, output, sampleRate, frequency);
	REQUIRE(transfer.gain == Catch::Approx(0.5).margin(1.0e-12));
	REQUIRE(transfer.phaseRadians == Catch::Approx(-0.25).margin(1.0e-12));
	const auto harmonics = vekt::audio_analysis::measureHarmonicPeakAmplitudes<3, double>(
		output, sampleRate, frequency);
	REQUIRE(harmonics[0] == Catch::Approx(0.5).margin(1.0e-12));
	REQUIRE(harmonics[1] == Catch::Approx(0.1).margin(1.0e-12));
	REQUIRE(harmonics[2] == Catch::Approx(0.0).margin(1.0e-12));
}

TEST_CASE("Audio analysis measures two-tone components and residual", "[audio-analysis]")
{
	constexpr double sampleRate = 48'000.0;
	constexpr double firstFrequency = 1'000.0;
	constexpr double secondFrequency = 1'300.0;
	std::vector<double> samples(static_cast<std::size_t>(sampleRate));
	for (std::size_t sample = 0; sample < samples.size(); ++sample)
	{
		const auto time = static_cast<double>(sample) / sampleRate;
		samples[sample] = 0.4 * std::sin(2.0 * std::numbers::pi * firstFrequency * time)
			+ 0.3 * std::sin(2.0 * std::numbers::pi * secondFrequency * time)
			+ 0.02 * std::sin(2.0 * std::numbers::pi * (2.0 * firstFrequency - secondFrequency) * time);
	}
	const auto components = vekt::audio_analysis::measureTwoToneComponents<double>(
		samples, sampleRate, firstFrequency, secondFrequency);
	REQUIRE(components.firstFundamental == Catch::Approx(0.4).margin(1.0e-12));
	REQUIRE(components.secondFundamental == Catch::Approx(0.3).margin(1.0e-12));
	REQUIRE(components.lowerThirdOrder == Catch::Approx(0.02).margin(1.0e-12));
	REQUIRE(components.upperThirdOrder == Catch::Approx(0.0).margin(1.0e-12));
	constexpr std::array retained { firstFrequency, secondFrequency, 2.0 * firstFrequency - secondFrequency };
	const auto residual = vekt::audio_analysis::measureComponentResidual<double>(
		samples, sampleRate, retained);
	REQUIRE(residual.rms == Catch::Approx(0.0).margin(1.0e-12));
	REQUIRE(residual.peak == Catch::Approx(0.0).margin(1.0e-11));
}

TEST_CASE("Audio analysis reports sample comparison differences", "[audio-analysis]")
{
	constexpr std::array first { 1.0f, 2.0f, 3.0f };
	constexpr std::array second { 1.0f, 2.5f, 3.0f };
	const auto comparison = vekt::audio_analysis::compareSamples<float, float>(first, second);
	REQUIRE_FALSE(comparison.exact);
	REQUIRE(comparison.comparedSamples == 3);
	REQUIRE(comparison.firstDifferentSample == 1);
	REQUIRE(comparison.maximumAbsoluteDifference == Catch::Approx(0.5));
	REQUIRE(comparison.rmsDifference == Catch::Approx(std::sqrt(0.25 / 3.0)));
}
