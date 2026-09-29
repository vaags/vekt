#include "LadderPoleMix.h"
#include "LadderResonance.h"
#include "NonlinearTptLadder.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <cmath>
#include <numbers>

namespace
{
using vekt::mono::ladderFeedbackGain;
using vekt::mono::ladderPoleMix;
using vekt::mono::ladderPoleMixTaps;
using vekt::mono::NonlinearTptLadder;
using vekt::mono::NonlinearTptLadderSettings;

constexpr double sampleRate = 48'000.0;

double decibels(double gain) { return 20.0 * std::log10(gain); }

// Small-signal gain of the real ladder at one frequency: settle, then correlate over whole periods.
double measureGain(float cutoffHz, float resonance, float mode, double frequency, double settleSeconds,
	double amplitude = 1.0e-3)
{
	NonlinearTptLadder ladder;
	ladder.prepare(sampleRate);
	const NonlinearTptLadderSettings settings { cutoffHz, resonance, 0.0f, false, 0.0f, mode };
	const auto settle = static_cast<int>(settleSeconds * sampleRate);
	const auto periods = std::max(1.0, std::round(0.25 * frequency));
	const auto window = static_cast<int>(std::round(periods * sampleRate / frequency));
	double inPhase {}, quadrature {};
	for (int sample = 0; sample < settle + window; ++sample)
	{
		const auto phase = 2.0 * std::numbers::pi * frequency * sample / sampleRate;
		const auto output = static_cast<double>(ladder.processCoupled(static_cast<float>(amplitude * std::sin(phase)), settings));
		if (sample < settle) continue;
		inPhase += output * std::sin(phase);
		quadrature += output * std::cos(phase);
	}
	return 2.0 * std::hypot(inPhase, quadrature) / window / amplitude;
}

// Settled output for a constant (DC) input.
double settledDc(float input, float resonance, float driveDecibels, float mode)
{
	NonlinearTptLadder ladder;
	ladder.prepare(sampleRate);
	const NonlinearTptLadderSettings settings { 1'000.0f, resonance, driveDecibels, false, 0.0f, mode };
	float output {};
	for (int sample = 0; sample < static_cast<int>(sampleRate); ++sample) output = ladder.processCoupled(input, settings);
	return output;
}
}

TEST_CASE("Mono ladder pole mix keeps both passbands at the ladder's own level", "[mono][filter][ladder-mode]")
{
	for (const auto k : { 0.0, 2.0, 3.0, 3.9, 4.6 })
	{
		const auto r = 1.0 / (1.0 + k);
		for (const auto mode : { -1.0, -0.6, -0.25, 0.0 })
		{
			// At DC every tap is u = r x, so the taps must sum to 1 for a low passband of r.
			const auto taps = ladderPoleMixTaps(mode, k);
			CHECK(taps[0] + taps[1] + taps[2] + taps[3] + taps[4] == Catch::Approx(1.0).margin(1.0e-12));
		}
		for (const auto mode : { 0.0, 0.3, 0.75, 1.0 })
			CHECK(ladderPoleMixTaps(mode, k)[0] == Catch::Approx(r).margin(1.0e-12));
	}
}

TEST_CASE("Mono ladder pole mix at LP returns stage four bit for bit", "[mono][filter][ladder-mode]")
{
	CHECK(std::bit_cast<std::uint32_t>(NonlinearTptLadderSettings {}.mode) == std::bit_cast<std::uint32_t>(-1.0f));
	for (const auto y4 : { 0.25, -0.0, 0.0, -3.5 })
	{
		const std::array<double, 4> stages { 0.9, -0.4, 0.7, y4 };
		CHECK(std::bit_cast<std::uint64_t>(ladderPoleMix(-1.0, 3.9, 12.0, stages)) == std::bit_cast<std::uint64_t>(y4));
	}
}

TEST_CASE("Mono ladder modes match their small-signal responses", "[mono][filter][ladder-mode]")
{
	for (const auto resonance : { 0.0f, 0.5f, 0.75f, 0.975f })
	{
		const auto k = ladderFeedbackGain(static_cast<double>(resonance));
		const auto expected = decibels(1.0 / (1.0 + k));
		INFO("resonance " << resonance << ", k " << k);
		for (const auto mode : { -1.0f, -0.5f, 0.0f })
		{
			INFO("low passband, mode " << mode);
			CHECK(decibels(measureGain(2'000.0f, resonance, mode, 20.0, 0.5)) == Catch::Approx(expected).margin(0.1));
		}
		for (const auto mode : { 0.0f, 0.5f, 1.0f })
		{
			INFO("high passband, mode " << mode);
			CHECK(decibels(measureGain(100.0f, resonance, mode, 20'000.0, 2.0)) == Catch::Approx(expected).margin(0.3));
		}
		// The notch zero sits at the (prewarped) stage pole, i.e. at Cutoff. The resonant gain there saturates
		// stage 1 early, so the depth shrinks 20 dB per decade of level; -80 dBFS keeps the test linear.
		INFO("notch depth");
		CHECK(decibels(measureGain(1'000.0f, resonance, 0.0f, 1'000.0, 1.0, 1.0e-4)) < expected - 60.0);
	}
}

TEST_CASE("Mono ladder HP and Notch keep their DC behaviour through saturating stages", "[mono][filter][ladder-mode]")
{
	for (const auto resonance : { 0.0f, 0.8f })
	{
		INFO("resonance " << resonance);
		const auto lowPass = settledDc(0.8f, resonance, 0.0f, -1.0f);
		// Every stage settles where tanh(input) = tanh(output), i.e. at u, so the HP taps cancel and the Notch passes
		// what LP passes. Heavy Drive breaks this in practice: far into tanh the stages creep towards u for seconds.
		CHECK(std::abs(settledDc(0.8f, resonance, 0.0f, 1.0f)) < 1.0e-5);
		CHECK(settledDc(0.8f, resonance, 0.0f, 0.0f) == Catch::Approx(lowPass).margin(1.0e-5));
	}
}
