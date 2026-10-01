#include <vekt/dsp/ScopeTap.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

TEST_CASE("ScopeTap reads the newest samples oldest first", "[dsp][scope]")
{
	vekt::dsp::ScopeTap tap;
	tap.prepare(48'000.0);
	REQUIRE(tap.getSampleRate() == 48'000.0);
	juce::AudioBuffer<float> buffer { 2, 3 };
	buffer.copyFrom(0, 0, std::array { 0.1f, 0.2f, std::numeric_limits<float>::quiet_NaN() }.data(), 3);
	buffer.copyFrom(1, 0, std::array { -0.1f, -0.2f, -0.3f }.data(), 3);
	tap.publish(buffer);

	std::vector<float> left(5), right(5);
	REQUIRE(tap.readLatest(left, right) == 3);
	// Before the first sample reads as silence; non-finite samples are stored as silence.
	REQUIRE(left == std::vector { 0.0f, 0.0f, 0.1f, 0.2f, 0.0f });
	REQUIRE(right == std::vector { 0.0f, 0.0f, -0.1f, -0.2f, -0.3f });
}

TEST_CASE("ScopeTap feeds both channels from a mono buffer and wraps around its capacity", "[dsp][scope]")
{
	vekt::dsp::ScopeTap tap;
	tap.prepare(44'100.0);
	juce::AudioBuffer<float> buffer { 1, 1000 };
	auto next = 0.0f;
	const auto blocks = vekt::dsp::ScopeTap::capacity / 1000 + 3;
	for (std::size_t block = 0; block < blocks; ++block)
	{
		for (auto sample = 0; sample < buffer.getNumSamples(); ++sample) buffer.setSample(0, sample, next++);
		tap.publish(buffer);
	}
	std::vector<float> left(4), right(4);
	REQUIRE(tap.readLatest(left, right) == blocks * 1000);
	REQUIRE(left == std::vector { next - 4.0f, next - 3.0f, next - 2.0f, next - 1.0f });
	REQUIRE(right == left);
}

TEST_CASE("Scope trigger holds a periodic signal still at its newest rising zero crossing", "[dsp][scope]")
{
	constexpr std::size_t window = 200;
	const auto period = 37.3;
	for (const auto phase : { 0.0, 1.0, 2.5 })
	{
		std::vector<float> samples(window * 2);
		for (std::size_t index = 0; index < samples.size(); ++index)
			samples[index] = static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * (static_cast<double>(index) / period) + phase));
		const auto start = vekt::dsp::findScopeTrigger(samples, window);
		REQUIRE(start <= window);
		REQUIRE(static_cast<double>(window - start) < period);
		REQUIRE(samples[start - 1] < 0.0f);
		REQUIRE(samples[start] >= 0.0f);
	}
}

TEST_CASE("Scope trigger ignores noise around zero and free-runs without a crossing", "[dsp][scope]")
{
	constexpr std::size_t window = 100;
	// A square wave whose low half carries tiny wiggles across zero: only the full swing up triggers.
	std::vector<float> samples(window * 2);
	for (std::size_t index = 0; index < samples.size(); ++index)
	{
		const auto phase = index % 50;
		samples[index] = phase < 25 ? -0.8f : (phase % 2 == 0 ? 0.01f : -0.01f);
	}
	const auto start = vekt::dsp::findScopeTrigger(samples, window);
	REQUIRE(start % 50 == 26);
	REQUIRE(start > window - 50);

	REQUIRE(vekt::dsp::findScopeTrigger(std::vector<float>(window * 2, 0.0f), window) == window);
	REQUIRE(vekt::dsp::findScopeTrigger(std::vector<float>(window * 2, 0.3f), window) == window);
	REQUIRE(vekt::dsp::findScopeTrigger(std::vector<float>(window / 2, 0.3f), window) == 0);
}
