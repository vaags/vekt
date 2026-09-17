#include <vekt/dsp/ThreeBandCrossover.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>

TEST_CASE("ThreeBandCrossover recombines a steady stereo signal", "[dsp][crossover]")
{
	vekt::dsp::ThreeBandCrossover<float> crossover;
	const juce::dsp::ProcessSpec specification { 48'000.0, 128, 2 };
	crossover.prepare(specification, { 250.0f, 2'500.0f });

	juce::AudioBuffer<float> input(2, 128);
	juce::AudioBuffer<float> low(2, 128);
	juce::AudioBuffer<float> mid(2, 128);
	juce::AudioBuffer<float> high(2, 128);
	input.clear();
	input.setSample(0, 100, 0.5f);
	input.setSample(1, 100, -0.25f);

	crossover.process(
		juce::dsp::AudioBlock<const float>(input),
		{ juce::dsp::AudioBlock<float>(low), juce::dsp::AudioBlock<float>(mid),
			juce::dsp::AudioBlock<float>(high) });

	for (auto sample = 32; sample < 128; ++sample)
	{
		REQUIRE(low.getSample(0, sample) + mid.getSample(0, sample) + high.getSample(0, sample)
			== Catch::Approx(input.getSample(0, sample)).margin(1.0e-4f));
		REQUIRE(low.getSample(1, sample) + mid.getSample(1, sample) + high.getSample(1, sample)
			== Catch::Approx(input.getSample(1, sample)).margin(1.0e-4f));
	}
}

TEST_CASE("ThreeBandCrossover cutoff requests are bounded and resettable", "[dsp][crossover]")
{
	vekt::dsp::ThreeBandCrossover<float> crossover;
	crossover.prepare({ 48'000.0, 64, 2 }, { 250.0f, 2'500.0f });
	crossover.requestCutoffs({ -1.0f, 1.0f });

	const auto requested = crossover.getRequestedCutoffs();
	REQUIRE(requested.lowMidHz >= 20.0f);
	REQUIRE(requested.midHighHz >= requested.lowMidHz * 2.0f - 1.0e-3f);

	crossover.reset();
	const auto active = crossover.getActiveCutoffs();
	REQUIRE(active.lowMidHz == Catch::Approx(requested.lowMidHz));
	REQUIRE(active.midHighHz == Catch::Approx(requested.midHighHz));
}
