#include <vekt/dsp/AdaptiveAutoGain.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_audio_basics/juce_audio_basics.h>

TEST_CASE("AdaptiveAutoGain attenuates a louder wet signal", "[processor][auto-gain]")
{
	juce::AudioBuffer<float> reference(2, 2'048);
	juce::AudioBuffer<float> wet(2, 2'048);
	for (auto channel = 0; channel < 2; ++channel)
		for (auto sample = 0; sample < 2'048; ++sample)
		{
			reference.setSample(channel, sample, 0.25f);
			wet.setSample(channel, sample, 0.5f);
		}

	vekt::dsp::AdaptiveAutoGain<float> autoGain;
	autoGain.prepare(48'000.0, 0.0);
	autoGain.process(juce::dsp::AudioBlock<const float>(reference),
		juce::dsp::AudioBlock<float>(wet), true);

	REQUIRE(wet.getSample(0, 2'047) == Catch::Approx(0.25f));
	REQUIRE(wet.getSample(1, 2'047) == Catch::Approx(0.25f));
}

TEST_CASE("AdaptiveAutoGain never boosts or changes disabled wet audio", "[processor][auto-gain]")
{
	juce::AudioBuffer<float> reference(2, 2'048);
	juce::AudioBuffer<float> wet(2, 2'048);
	reference.clear();
	wet.setSample(0, 0, 0.25f);
	wet.setSample(1, 0, -0.25f);
	for (auto sample = 1; sample < 2'048; ++sample)
	{
		wet.setSample(0, sample, 0.25f);
		wet.setSample(1, sample, -0.25f);
	}

	vekt::dsp::AdaptiveAutoGain<float> autoGain;
	autoGain.prepare(48'000.0, 0.0);
	autoGain.process(juce::dsp::AudioBlock<const float>(reference),
		juce::dsp::AudioBlock<float>(wet), true);
	REQUIRE(wet.getSample(0, 2'047) <= 0.25f);

	vekt::dsp::AdaptiveAutoGain<float> disabledAutoGain;
	disabledAutoGain.prepare(48'000.0, 0.0);
	const auto disabledReference = wet.getSample(0, 2'047);
	juce::AudioBuffer<float> disabledWet(wet);
	disabledAutoGain.process(juce::dsp::AudioBlock<const float>(reference),
		juce::dsp::AudioBlock<float>(disabledWet), false);
	REQUIRE(disabledWet.getSample(0, 2'047) == Catch::Approx(disabledReference));
}
