#include "NonlinearTptLadder.h"

#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace
{
using vekt::mono::NonlinearTptLadder;
using vekt::mono::NonlinearTptLadderSettings;

struct Scenario
{
	const char* name;
	std::size_t lanes;
	double sampleRate;
	float resonance, driveDecibels, compensation;
	float startCutoff, endCutoff;
	double seconds;
	double burstSeconds; // input stops after this (self-oscillation); <= 0 keeps the saws running
};

struct Difference
{
	double maximumDb {}, rmsDb {};
	std::uint64_t unconverged {}, nonFinite {};
};

// Feeds identical detuned saws to scalar and batched ladders and returns their difference relative to the
// scalar output's RMS.
Difference compare(const Scenario& scenario)
{
	std::array<NonlinearTptLadder, 4> scalar, batched;
	for (std::size_t lane = 0; lane < scenario.lanes; ++lane)
	{
		scalar[lane].prepare(scenario.sampleRate);
		batched[lane].prepare(scenario.sampleRate);
	}
	const auto samples = static_cast<int>(scenario.seconds * scenario.sampleRate);
	std::array<double, 4> phase { 0.0, 0.31, 0.57, 0.83 };
	std::array<float, 4> inputs {}, batchedOutputs {};
	double signal {}, differenceSum {}, maximumDifference {};
	for (int sample = 0; sample < samples; ++sample)
	{
		const auto time = static_cast<double>(sample) / scenario.sampleRate;
		const auto progress = static_cast<float>(sample) / static_cast<float>(samples);
		const NonlinearTptLadderSettings settings { scenario.startCutoff * std::pow(scenario.endCutoff / scenario.startCutoff, progress),
			scenario.resonance, scenario.driveDecibels, true, scenario.compensation };
		const auto playing = scenario.burstSeconds <= 0.0 || time < scenario.burstSeconds;
		for (std::size_t lane = 0; lane < scenario.lanes; ++lane)
		{
			const auto frequency = 110.0 * std::exp2((static_cast<double>(lane) - 1.5) * 0.15 / 12.0);
			phase[lane] += frequency / scenario.sampleRate;
			phase[lane] -= std::floor(phase[lane]);
			inputs[lane] = playing ? static_cast<float>(0.5 * (2.0 * phase[lane] - 1.0)) : 0.0f;
		}
		NonlinearTptLadder::processCoupled(std::span(batched.data(), scenario.lanes), std::span<const float>(inputs.data(), scenario.lanes),
			std::span(batchedOutputs.data(), scenario.lanes), settings);
		for (std::size_t lane = 0; lane < scenario.lanes; ++lane)
		{
			const auto reference = static_cast<double>(scalar[lane].processCoupled(inputs[lane], settings));
			const auto difference = std::abs(static_cast<double>(batchedOutputs[lane]) - reference);
			signal += reference * reference;
			differenceSum += difference * difference;
			maximumDifference = std::max(maximumDifference, difference);
		}
	}
	const auto count = static_cast<double>(samples) * static_cast<double>(scenario.lanes);
	const auto signalRms = std::sqrt(signal / count);
	const auto toDb = [signalRms](double value) { return value > 0.0 ? 20.0 * std::log10(value / signalRms) : -400.0; };
	Difference result { toDb(maximumDifference), toDb(std::sqrt(differenceSum / count)) };
	for (std::size_t lane = 0; lane < scenario.lanes; ++lane)
		for (const auto* ladder : { &scalar[lane], &batched[lane] })
		{
			result.unconverged += ladder->diagnostics().unconvergedSamples;
			result.nonFinite += ladder->diagnostics().nonFiniteSamples;
		}
	return result;
}
}

TEST_CASE("Mono batched ladder lanes match the scalar solver far below audibility", "[mono][filter][ladder-coupled][slow]")
{
	const std::array scenarios {
		Scenario { "2 lanes, gentle sweep", 2, 48'000.0, 0.3f, 0.0f, 0.0f, 200.0f, 8'000.0f, 1.0, 0.0 },
		Scenario { "4 lanes, resonant driven sweep", 4, 48'000.0, 0.85f, 12.0f, 0.5f, 8'000.0f, 150.0f, 1.0, 0.0 },
		Scenario { "4 lanes, 4x rate, near self-oscillation", 4, 192'000.0, 0.95f, 24.0f, 0.0f, 600.0f, 3'000.0f, 1.0, 0.0 },
		Scenario { "4 lanes, 10 s self-oscillation at full resonance and drive", 4, 48'000.0, 1.0f, 24.0f, 0.5f, 1'000.0f, 1'000.0f, 10.0, 0.05 },
	};
	for (const auto& scenario : scenarios)
	{
		CAPTURE(scenario.name);
		const auto difference = compare(scenario);
		WARN(scenario.name << ": max " << difference.maximumDb << " dB, RMS " << difference.rmsDb << " dB re signal RMS");
		REQUIRE(difference.nonFinite == 0);
		REQUIRE(difference.unconverged == 0);
		// Far below 24-bit resolution (about -144 dBFS) and any converter noise floor.
		REQUIRE(difference.maximumDb < -120.0);
	}
}

TEST_CASE("Mono batched ladders prepared at different sample rates match their scalar solves exactly", "[mono][filter][ladder-coupled]")
{
	// The batched solve shares one integration gain; mixed rates must fall back to per-ladder solves.
	constexpr std::array rates { 48'000.0, 96'000.0, 48'000.0, 192'000.0 };
	for (const auto lanes : { std::size_t { 2 }, std::size_t { 4 } })
	{
		CAPTURE(lanes);
		std::array<NonlinearTptLadder, 4> scalar, batched;
		for (std::size_t lane = 0; lane < lanes; ++lane)
		{
			scalar[lane].prepare(rates[lane]);
			batched[lane].prepare(rates[lane]);
		}
		const NonlinearTptLadderSettings settings { 2'000.0f, 0.9f, 12.0f, true, 0.5f };
		std::array<float, 4> inputs {}, outputs {};
		for (int sample = 0; sample < 4'800; ++sample)
		{
			for (std::size_t lane = 0; lane < lanes; ++lane)
				inputs[lane] = static_cast<float>(0.5 * std::sin(0.01 * sample * static_cast<double>(lane + 1)));
			NonlinearTptLadder::processCoupled(std::span(batched.data(), lanes), std::span<const float>(inputs.data(), lanes),
				std::span(outputs.data(), lanes), settings);
			for (std::size_t lane = 0; lane < lanes; ++lane)
				REQUIRE(juce::exactlyEqual(outputs[lane], scalar[lane].processCoupled(inputs[lane], settings)));
		}
	}
}
