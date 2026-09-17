#include <vekt/dsp/MatchedToneStage.h>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("MatchedToneStage accepts a slower modulation ramp", "[dsp][tone]")
{
	vekt::dsp::MatchedToneStage<float> tone;
	tone.prepare(48'000.0, 2);
	tone.setRampDurationSeconds(0.15);
	tone.setSlopeDbPerOctave(6.0f);

	juce::AudioBuffer<float> buffer(2, 128);
	buffer.clear();
	tone.processPre(juce::dsp::AudioBlock<float>(buffer));

	for (auto channel = 0; channel < buffer.getNumChannels(); ++channel)
		for (auto sample = 0; sample < buffer.getNumSamples(); ++sample)
			REQUIRE(std::isfinite(buffer.getSample(channel, sample)));
}
