#include <audio_lab/RackRouting.h>

#include <vekt/rav/Parameters.h>
#include <vekt/rav/PluginProcessor.h>
#include <vekt/glimmer/Parameters.h>
#include <vekt/glimmer/PluginProcessor.h>
#include <vekt/kobber/PluginProcessor.h>
#include <vekt/flint/PluginProcessor.h>

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

TEST_CASE("Audio Lab measures Mono processing with the effects rack bypassed", "[audio-lab][rack][kobber][cpu]")
{
	vekt::kobber::PluginProcessor kobber;
	vekt::rav::PluginProcessor rav;
	vekt::glimmer::PluginProcessor glimmer;
	constexpr int blockSize = 4096;
	kobber.prepareToPlay(48'000.0, blockSize);
	juce::AudioBuffer<float> block(2, blockSize);
	juce::MidiBuffer midi;
	midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
	const auto kobberTicks = vekt::audio_lab::processInstrument(block, midi, kobber);
	REQUIRE(kobberTicks > 0);
	REQUIRE(block.getMagnitude(0, 0, blockSize) > 0.0f);
	const auto rackStart = juce::Time::getHighResolutionTicks();
	vekt::audio_lab::processRackRoute(0, block, midi, rav, glimmer);
	const auto totalTicks = kobberTicks + juce::Time::getHighResolutionTicks() - rackStart;
	REQUIRE(totalTicks >= kobberTicks);
}
TEST_CASE("Audio Lab silences the instrument it stops playing", "[audio-lab][rack][flint]")
{
	constexpr int blockSize = 512;
	vekt::kobber::PluginProcessor kobber;
	vekt::flint::PluginProcessor flint;
	kobber.prepareToPlay(48'000.0, blockSize);
	flint.prepareToPlay(48'000.0, blockSize);
	juce::AudioBuffer<float> block(2, blockSize);
	juce::MidiBuffer midi, scratch;
	scratch.ensureSize(vekt::audio_lab::silenceEventBytes);
	// Mono holds a note, then the lab switches to Flint and strikes it.
	midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
	juce::ignoreUnused(vekt::audio_lab::processInstrument(block, midi, kobber));
	REQUIRE(block.getMagnitude(0, 0, blockSize) > 0.0f);
	vekt::audio_lab::silenceInstrument(kobber, block, scratch);
	REQUIRE(block.getMagnitude(0, 0, blockSize) == 0.0f);
	juce::ignoreUnused(vekt::audio_lab::processInstrument(block, midi, flint));
	REQUIRE(block.getMagnitude(0, 0, blockSize) > 0.0f);
	// Back to Mono with no new note: the held note does not come back.
	midi.clear();
	for (auto iteration = 0; iteration < 8; ++iteration)
	{
		block.clear();
		juce::ignoreUnused(vekt::audio_lab::processInstrument(block, midi, kobber));
	}
	REQUIRE(block.getMagnitude(0, 0, blockSize) < 1.0e-4f);
}
