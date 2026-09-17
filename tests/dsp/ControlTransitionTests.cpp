#include <vekt/dsp/ControlTransition.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("ControlTransition reaches its target without overshoot", "[dsp][control]")
{
	vekt::dsp::ControlTransition<float> transition;
	transition.prepare(48'000.0, 0.01, 0.5);
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
