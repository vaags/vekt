#include "LadderPoleMix.h"
#include "LadderResonance.h"
#include "NonlinearTptLadder.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <cmath>
#include <numbers>
#include <span>

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

// Fundamental gain of the real ladder for a sine at a musical level, after one second of settling.
double drivenGain(float resonance, float driveDecibels, float mode, double frequency, double amplitude = 0.5)
{
	NonlinearTptLadder ladder;
	ladder.prepare(sampleRate);
	const NonlinearTptLadderSettings settings { 1'000.0f, resonance, driveDecibels, false, 0.0f, mode };
	const auto settle = static_cast<int>(sampleRate);
	const auto window = static_cast<int>(std::round(std::round(frequency) * sampleRate / frequency));
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

TEST_CASE("Mono ladder pole mix eases into each landmark", "[mono][filter][ladder-mode]")
{
	// The high-band tap leaves LP quadratically (smoothstep), not linearly, and meets each landmark with zero slope.
	const auto r = 1.0 / (1.0 + 3.0);
	CHECK(ladderPoleMixTaps(-0.9, 3.0)[0] == Catch::Approx(r * 0.028).margin(1.0e-12));
	for (const auto landmark : { -1.0, 0.0, 1.0 })
		for (std::size_t tap = 0; tap < 5; ++tap)
		{
			const auto below = ladderPoleMixTaps(landmark - 1.0e-4, 3.0)[tap], at = ladderPoleMixTaps(landmark, 3.0)[tap];
			const auto above = ladderPoleMixTaps(landmark + 1.0e-4, 3.0)[tap];
			if (landmark > -1.0) CHECK(std::abs(at - below) < 1.0e-6);
			if (landmark < 1.0) CHECK(std::abs(above - at) < 1.0e-6);
		}
}

TEST_CASE("Mono ladder pole mix at LP returns stage four bit for bit", "[mono][filter][ladder-mode]")
{
	CHECK(std::bit_cast<std::uint32_t>(NonlinearTptLadderSettings {}.mode) == std::bit_cast<std::uint32_t>(-1.0f));
	for (const auto y4 : { 0.25, -0.0, 0.0, -3.5 })
	{
		const std::array<double, 4> stages { 0.9, -0.4, 0.7, y4 };
		CHECK(std::bit_cast<std::uint64_t>(ladderPoleMix(-1.0, 3.9, 12.0, stages, 1.0, 0.5, stages)) == std::bit_cast<std::uint64_t>(y4));
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
		// Every stage settles where tanh(input) = tanh(output), i.e. at u (which LP outputs), so every tap is the
		// same a tanh(u / a): the HP taps cancel and the Notch taps (summing to 1) give a tanh(u / a), a = 3 at no Drive.
		CHECK(std::abs(settledDc(0.8f, resonance, 0.0f, 1.0f)) < 1.0e-5);
		CHECK(settledDc(0.8f, resonance, 0.0f, 0.0f) == Catch::Approx(3.0 * std::tanh(lowPass / 3.0)).margin(1.0e-5));
	}
}

TEST_CASE("Mono ladder keeps bass out of HP and bounds Notch/HP under heavy drive", "[mono][filter][ladder-mode]")
{
	// Raw taps let a 20 Hz tone through HP above input level at +24 dB Drive, and let HP at 6 kHz follow the full
	// Drive; the saturated taps must do neither.
	for (const auto resonance : { 0.0f, 0.5f })
	{
		INFO("resonance " << resonance);
		// The knee narrows with Drive, so bass stays out wherever raw taps would leak (from about +15 dB).
		for (const auto drive : { 12.0f, 15.0f, 18.0f, 24.0f })
		{
			INFO("drive " << drive);
			CHECK(decibels(drivenGain(resonance, drive, 1.0f, 20.0)) < -40.0);
		}
		const auto lowPass = decibels(drivenGain(resonance, 24.0f, -1.0f, 100.0));
		for (const auto [mode, frequency] : { std::pair { 0.0f, 100.0 }, std::pair { 1.0f, 6'000.0 } })
			CHECK(decibels(drivenGain(resonance, 24.0f, mode, frequency)) < lowPass);
	}
}

TEST_CASE("Mono batched ladders mix Notch/HP like the scalar solve", "[mono][filter][ladder-mode][ladder-coupled]")
{
	std::array<NonlinearTptLadder, 4> scalar, batched;
	for (auto& ladder : scalar) ladder.prepare(sampleRate);
	for (auto& ladder : batched) ladder.prepare(sampleRate);
	const NonlinearTptLadderSettings settings { 900.0f, 0.6f, 18.0f, false, 0.0f, 0.4f };
	std::array<float, 4> inputs {}, outputs {};
	double maximumDifference {};
	for (int sample = 0; sample < 9'600; ++sample)
	{
		for (std::size_t lane = 0; lane < 4; ++lane)
			inputs[lane] = static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * (110.0 + 3.0 * static_cast<double>(lane)) * sample / sampleRate));
		NonlinearTptLadder::processCoupled(std::span(batched), std::span<const float>(inputs), std::span(outputs), settings);
		for (std::size_t lane = 0; lane < 4; ++lane)
			maximumDifference = std::max(maximumDifference,
				static_cast<double>(std::abs(scalar[lane].processCoupled(inputs[lane], settings) - outputs[lane])));
	}
	// Only the vector tanh (about 2 ulp from libm) differs.
	CHECK(maximumDifference < 1.0e-5);
}

TEST_CASE("Mono ladder HP barely compresses a hot mixer without Drive", "[mono][filter][ladder-mode]")
{
	// A full three-oscillator mix reaches the ladder at about this level. With a = 1 the HP lost 3-5 dB more than
	// raw taps here; with the Drive-following knee it stays within 2.5 dB of the small-signal response (worst,
	// -1.8 dB, just above Cutoff where the stages themselves saturate).
	for (const auto resonance : { 0.0f, 0.5f })
		for (const auto frequency : { 3'000.0, 8'000.0 })
		{
			INFO("resonance " << resonance << ", frequency " << frequency);
			const auto linear = decibels(measureGain(1'000.0f, resonance, 1.0f, frequency, 1.0));
			CHECK(decibels(drivenGain(resonance, 0.0f, 1.0f, frequency, 2.0)) > linear - 2.5);
		}
}

TEST_CASE("Mono ladder Notch/HP track LP whatever the resonance under heavy drive", "[mono][filter][ladder-mode]")
{
	// Saturation takes away the feedback's bass loss from LP; the saturated taps follow it (ladderFeedbackAuthority)
	// instead of keeping the small-signal 1 / (1 + k), which left HP 10-17 dB under LP at high resonance.
	const auto gap = [](float resonance, float drive, float mode, double frequency, double amplitude)
	{
		return decibels(drivenGain(resonance, drive, mode, frequency, amplitude))
			- decibels(drivenGain(resonance, drive, -1.0f, 100.0, amplitude));
	};
	for (const auto amplitude : { 0.25, 1.0 })
		for (const auto drive : { 12.0f, 18.0f, 24.0f })
			for (const auto [mode, frequency] : { std::pair { 0.0f, 6'000.0 }, std::pair { 1.0f, 6'000.0 } })
			{
				const auto reference = gap(0.0f, drive, mode, frequency, amplitude);
				for (const auto resonance : { 0.5f, 0.9f })
				{
					INFO("amplitude " << amplitude << ", drive " << drive << ", mode " << mode << ", resonance " << resonance);
					const auto measured = gap(resonance, drive, mode, frequency, amplitude);
					CHECK(std::abs(measured - reference) < 2.0);
					CHECK(measured > -5.0);
				}
			}
}
