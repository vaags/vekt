#include "../../tools/audio_lab/RackRouting.h"

#include <Parameters.h>
#include <PluginProcessor.h>
#include <vekt/glimmer/Parameters.h>
#include <vekt/glimmer/PluginProcessor.h>
#include <vekt/mono/PluginProcessor.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>

TEST_CASE("Audio Lab rack routes send source audio through the selected effects", "[audio-lab][rack]")
{
	constexpr int blockSize = 256;
	constexpr double sampleRate = 48'000.0;
	for (int route = 0; route < 5; ++route)
	{
		vekt::rav::PluginProcessor rav;
		vekt::glimmer::PluginProcessor glimmer;
		const auto setGain = [](juce::AudioProcessorValueTreeState& state, const char* id)
		{
			auto* parameter = state.getParameter(id);
			REQUIRE(parameter != nullptr);
			parameter->setValueNotifyingHost(parameter->convertTo0to1(-24.0f));
		};
		setGain(rav.getParameters(), vekt::rav::parameters::outputGain);
		setGain(glimmer.getParameters(), vekt::glimmer::parameters::outputGain);
		rav.prepareToPlay(sampleRate, blockSize);
		glimmer.prepareToPlay(sampleRate, blockSize);
		juce::AudioBuffer<float> storage(2, blockSize + 17);
		juce::MidiBuffer midi;
		double squares {};
		for (int iteration = 0; iteration < 80; ++iteration)
		{
			storage.clear();
			for (int sample = 0; sample < blockSize; ++sample)
			{
				const auto value = 0.1f * static_cast<float>(std::sin(
					2.0 * juce::MathConstants<double>::pi * 440.0 * (iteration * blockSize + sample) / sampleRate));
				storage.setSample(0, sample + 17, value);
				storage.setSample(1, sample + 17, value);
			}
			juce::AudioBuffer<float> block(storage.getArrayOfWritePointers(), 2, 17, blockSize);
			vekt::audio_lab::processRackRoute(route, block, midi, rav, glimmer);
			for (int sample = 0; sample < blockSize; ++sample)
				squares += static_cast<double>(block.getSample(0, sample)) * block.getSample(0, sample);
		}
		const auto rms = std::sqrt(squares / (80 * blockSize));
		CAPTURE(route, rms);
		if (route == 0) REQUIRE(rms > 0.06);
		else if (route == 1 || route == 2) REQUIRE(rms < 0.025);
		else REQUIRE(rms < 0.01);
	}
}

TEST_CASE("Audio Lab measures Mono processing with the effects rack bypassed", "[audio-lab][rack][mono][cpu]")
{
	vekt::mono::PluginProcessor mono;
	vekt::rav::PluginProcessor rav;
	vekt::glimmer::PluginProcessor glimmer;
	constexpr int blockSize = 4096;
	mono.prepareToPlay(48'000.0, blockSize);
	juce::AudioBuffer<float> block(2, blockSize);
	juce::MidiBuffer midi;
	midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
	const auto monoTicks = vekt::audio_lab::processMonoSource(block, midi, mono);
	REQUIRE(monoTicks > 0);
	REQUIRE(block.getMagnitude(0, 0, blockSize) > 0.0f);
	const auto rackStart = juce::Time::getHighResolutionTicks();
	vekt::audio_lab::processRackRoute(0, block, midi, rav, glimmer);
	const auto totalTicks = monoTicks + juce::Time::getHighResolutionTicks() - rackStart;
	REQUIRE(totalTicks >= monoTicks);
}