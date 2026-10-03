#include <vekt/dsp/ControlTransition.h>
#include <vekt/dsp/LinearRamp.h>
#include <vekt/dsp/OversamplingQuality.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

TEST_CASE("ControlTransition reaches its target without overshoot", "[dsp][control]")
{
	vekt::dsp::ControlTransition<float> transition;
	transition.prepare(48'000.0, 0.01, 0.5, 1.0);
	transition.setCurrentAndTargetValue(0.0f);
	transition.setTargetValue(1.0f);

	float previous = transition.getCurrentValue();
	for (auto sample = 0; sample < 48'000; ++sample)
	{
		const auto current = transition.getNextValue();
		REQUIRE(current >= previous);
		REQUIRE(current <= 1.0f);
		previous = current;
	}
	REQUIRE(previous == Catch::Approx(1.0f));
}

TEST_CASE("ControlTransition retargets from its current value", "[dsp][control]")
{
	vekt::dsp::ControlTransition<float> transition;
	transition.prepare(48'000.0, 0.01, 0.5);
	transition.setCurrentAndTargetValue(0.0f);
	transition.setTargetValue(1.0f);
	for (auto sample = 0; sample < 100; ++sample)
		juce::ignoreUnused(transition.getNextValue());
	const auto beforeRetarget = transition.getCurrentValue();
	transition.setTargetValue(-1.0f);
	REQUIRE(transition.getCurrentValue() == Catch::Approx(beforeRetarget));
}

TEST_CASE("ControlTransition artifact-safe policy takes longer", "[dsp][control]")
{
	vekt::dsp::ControlTransition<float> transition;
	transition.prepare(48'000.0, 0.01, 0.5);
	transition.setCurrentAndTargetValue(0.0f);
	transition.setPolicy(vekt::dsp::ControlTransitionPolicy::artifactSafe);
	transition.setTargetValue(1.0f);
	for (auto sample = 0; sample < 480; ++sample)
		juce::ignoreUnused(transition.getNextValue());
	REQUIRE(transition.getCurrentValue() < 1.0f);
}

TEST_CASE("ControlTransition does not restart on an unchanged target", "[dsp][control]")
{
	vekt::dsp::ControlTransition<float> transition;
	transition.prepare(48'000.0, 0.01, 0.5);
	transition.setCurrentAndTargetValue(0.0f);
	transition.setTargetValue(1.0f);
	for (auto sample = 0; sample < 480; ++sample)
	{
		if (sample % 16 == 0)
			transition.setTargetValue(1.0f);
		juce::ignoreUnused(transition.getNextValue());
	}
	REQUIRE(transition.getCurrentValue() > 0.0f);
}

TEST_CASE("ControlTransition ramps small moves smoothly at the highest internal rate", "[dsp][control][precision]")
{
	// A 0.02 bias move on the 1 s operating-point ramp at 192 kHz x16: each step (6.5e-9) is below half a float ulp at
	// 0.3, so a float ramp held still for a second and then jumped. Half way through it must be half way.
	vekt::dsp::ControlTransition<float> transition;
	transition.prepare(vekt::dsp::maximumInternalSampleRate, 0.02, 0.5, 1.0);
	transition.setPolicy(vekt::dsp::ControlTransitionPolicy::operatingPoint);
	transition.setCurrentAndTargetValue(0.3f);
	transition.setTargetValue(0.32f);
	float value {};
	for (int sample = 0; sample < static_cast<int>(0.5 * vekt::dsp::maximumInternalSampleRate); ++sample)
		value = transition.getNextValue();
	CHECK(std::abs(value - 0.31f) < 0.001f);
}

TEST_CASE("LinearRamp moves small steps smoothly at the highest internal rate", "[dsp][control][precision]")
{
	// Mono's cutoff ramp (in octaves, about 13 for 8 kHz) over 15 ms: a 0.01-octave move takes 2.2e-7 per sample at
	// 192 kHz x16, under half a float ulp at 13, so a float ramp stalled and then stepped 12 cents. Half way through
	// it must be half way.
	vekt::dsp::LinearRamp ramp;
	ramp.reset(vekt::dsp::maximumInternalSampleRate, 0.015);
	ramp.setCurrentAndTargetValue(13.0);
	ramp.setTargetValue(13.01);
	double value {};
	for (int sample = 0; sample < static_cast<int>(0.0075 * vekt::dsp::maximumInternalSampleRate); ++sample)
		value = ramp.getNextValue();
	CHECK(std::abs(value - 13.005) < 1.0e-4);
	juce::SmoothedValue<float> single;
	single.reset(vekt::dsp::maximumInternalSampleRate, 0.015);
	single.setCurrentAndTargetValue(13.0f);
	single.setTargetValue(13.01f);
	float stalled {};
	for (int sample = 0; sample < static_cast<int>(0.0075 * vekt::dsp::maximumInternalSampleRate); ++sample)
		stalled = single.getNextValue();
	CHECK(std::abs(stalled - 13.005f) > 1.0e-3f); // why the ramp is double: float has not moved
}
