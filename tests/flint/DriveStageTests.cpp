#include "DriveStage.h"
#include "SafetyClip.h"

#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace
{
using vekt::flint::DriveStage;
using vekt::flint::DriveType;

constexpr double sampleRate = 44'100.0;
constexpr std::array driveTypes { DriveType::soft, DriveType::hard, DriveType::fold };

std::vector<float> sine(std::size_t count, double frequency, double peak)
{
	std::vector<float> samples(count);
	for (std::size_t index = 0; index < count; ++index)
		samples[index] = static_cast<float>(
		    peak * std::sin(2.0 * std::numbers::pi * frequency * static_cast<double>(index) / sampleRate));
	return samples;
}

// Runs a stereo copy of `input` through the stage in 64-sample blocks; returns the left channel.
std::vector<float> run(DriveStage& stage, const std::vector<float>& input)
{
	auto left = input, right = input;
	for (std::size_t start = 0; start < left.size(); start += 64)
	{
		const auto count = std::min<std::size_t>(64, left.size() - start);
		stage.process(std::span(left).subspan(start, count), std::span(right).subspan(start, count));
	}
	return left;
}

float largestStep(const std::vector<float>& samples, std::size_t begin, std::size_t end)
{
	auto step = 0.0f;
	for (auto index = std::max<std::size_t>(begin, 1); index < end; ++index)
		step = std::max(step, std::abs(samples[index] - samples[index - 1]));
	return step;
}
}

TEST_CASE("Flint drive at zero leaves the signal bit-identical for every drive type", "[flint][drive]")
{
	const auto input = sine(4'096, 110.0, 0.5);
	for (const auto type : driveTypes)
	{
		DriveStage stage;
		stage.prepare(sampleRate);
		stage.setTarget(0.0, type);
		REQUIRE(stage.isBypassed());
		const auto output = run(stage, input);
		REQUIRE(std::equal(
		    output.begin(), output.end(), input.begin(), [](float a, float b) { return juce::exactlyEqual(a, b); }));
	}
}

TEST_CASE("Flint drive keeps a -6 dBFS peak at its level for every drive type", "[flint][drive]")
{
	// A 110 Hz sine peaking at 0.5 (-6 dBFS) keeps that peak within 0.5 dB at every Drive: the anti-aliased curve
	// averages over one sample, so the measured peak sits slightly below the curve's exact value.
	const auto input = sine(8'820, 110.0, 0.5);
	for (const auto type : driveTypes)
		for (const auto drive : { 0.25, 0.5, 0.75, 1.0 })
		{
			DriveStage stage;
			stage.prepare(sampleRate);
			stage.setTarget(drive, type);
			const auto output = run(stage, input);
			const auto settled = std::span(output).subspan(4'410); // past the 5 ms crossfade and 20 ms Drive ramp
			const auto peak = std::abs(*std::max_element(
			    settled.begin(), settled.end(), [](float a, float b) { return std::abs(a) < std::abs(b); }));
			INFO("type " << static_cast<int>(type) << ", drive " << drive << ", peak " << peak);
			REQUIRE(std::abs(20.0 * std::log10(static_cast<double>(peak) / 0.5)) < 0.5);
		}
}

TEST_CASE("Flint drive stays finite and bounded at every drive for every drive type", "[flint][drive]")
{
	// Including Fold's G = 2, where a curve normalized by f(G / 2) alone would divide by sin(pi) = 0.
	juce::Random random(12'345);
	std::vector<float> input(2'048);
	for (auto& sample : input) sample = random.nextFloat() * 2.0f - 1.0f;
	for (const auto type : driveTypes)
		for (auto step = 1; step <= 100; ++step)
		{
			DriveStage stage;
			stage.prepare(sampleRate);
			stage.setTarget(static_cast<double>(step) / 100.0, type);
			const auto output = run(stage, input);
			REQUIRE(std::all_of(
			    output.begin(), output.end(), [](float x) { return std::isfinite(x) && std::abs(x) < 4.0f; }));
		}
}

TEST_CASE("Flint drive fades in and out without a step when it crosses zero", "[flint][drive]")
{
	// Drive switched on mid-signal and off again: no step larger than the dry or fully driven signal's own largest
	// step, with a 10 % margin for the crossfade's blend of the two.
	const auto input = sine(13'230, 220.0, 0.5);
	DriveStage reference;
	reference.prepare(sampleRate);
	reference.setTarget(0.6, DriveType::hard);
	const auto driven = run(reference, input);
	const auto ownStep = std::max(largestStep(input, 0, input.size()), largestStep(driven, 4'410, driven.size()));

	DriveStage stage;
	stage.prepare(sampleRate);
	auto left = input, right = input;
	for (std::size_t start = 0; start < left.size(); start += 64)
	{
		if (start == 4'416) stage.setTarget(0.6, DriveType::hard);
		if (start == 8'832) stage.setTarget(0.0, DriveType::hard);
		const auto count = std::min<std::size_t>(64, left.size() - start);
		stage.process(std::span(left).subspan(start, count), std::span(right).subspan(start, count));
	}
	REQUIRE(largestStep(left, 0, left.size()) <= 1.1f * ownStep);
	REQUIRE(stage.isBypassed());
}

TEST_CASE("Flint safety clip is the identity up to -1 dBFS and never exceeds 0 dBFS", "[flint][drive]")
{
	using vekt::flint::safetyClip;
	using vekt::flint::safetyClipThreshold;
	auto previous = 0.0;
	for (auto step = 0; step <= 4'000; ++step)
	{
		const auto input = static_cast<double>(step) / 1'000.0;
		const auto output = safetyClip(input);
		if (input <= safetyClipThreshold) REQUIRE(juce::exactlyEqual(output, input));
		REQUIRE(output <= 1.0);
		REQUIRE(output >= previous);
		REQUIRE(juce::exactlyEqual(safetyClip(-input), -output));
		previous = output;
	}
	REQUIRE(juce::exactlyEqual(safetyClip(4.0), 1.0));
}
