#include <vekt/mono/PluginProcessor.h>

#include <vekt/presets/FilePresetRepository.h>
#include <vekt/presets/PresetSchema.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>

namespace
{
namespace parameters = vekt::mono::parameters;

void setParameter(vekt::mono::PluginProcessor& processor, const char* identifier, float value)
{
	auto* parameter = processor.getParameters().getParameter(identifier);
	parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

// Renders `samples` in 512-sample blocks, starting `note` at the first sample (or nothing if note < 0).
juce::AudioBuffer<float> play(vekt::mono::PluginProcessor& processor, int samples, int note, bool release = false)
{
	juce::AudioBuffer<float> output(2, samples);
	juce::AudioBuffer<float> block(2, 512);
	for (int start = 0; start < samples; start += 512)
	{
		juce::MidiBuffer midi;
		if (start == 0 && note >= 0) midi.addEvent(juce::MidiMessage::noteOn(1, note, 0.8f), 0);
		if (release && start + 512 >= samples && note >= 0) midi.addEvent(juce::MidiMessage::noteOff(1, note), 511);
		block.clear();
		processor.processBlock(block, midi);
		for (int channel = 0; channel < 2; ++channel)
			output.copyFrom(channel, start, block, channel, 0, std::min(512, samples - start));
	}
	return output;
}

bool identical(const juce::AudioBuffer<float>& first, const juce::AudioBuffer<float>& second)
{
	for (int channel = 0; channel < 2; ++channel)
		for (int sample = 0; sample < first.getNumSamples(); ++sample)
			if (!juce::exactlyEqual(first.getSample(channel, sample), second.getSample(channel, sample))) return false;
	return true;
}

struct TemporaryDirectory
{
	juce::File path = juce::File::getSpecialLocation(juce::File::tempDirectory)
		.getChildFile("vekt-mono-reproducibility-" + juce::Uuid().toString()); // unique across parallel test processes
	~TemporaryDirectory() { path.deleteRecursively(); }
};
}

TEST_CASE("Kobber repeated notes are not identical", "[kobber][processor][reproducibility]")
{
	vekt::mono::PluginProcessor processor;
	setParameter(processor, parameters::performanceMode, 0.0f);
	processor.prepareToPlay(48'000.0, 512);
	const auto first = play(processor, 9'728, 57, true);
	play(processor, 96'256, -1); // let the release finish so the next note starts on an idle voice
	const auto second = play(processor, 9'728, 57, true);
	REQUIRE(first.getRMSLevel(0, 0, 9'728) > 0.01f);
	REQUIRE_FALSE(identical(first, second));
}

TEST_CASE("Kobber preset loads give reproducible output whatever played before", "[kobber][processor][reproducibility][preset]")
{
	// A user preset exercising every stateful source: free-running LFO and vibrato clocks, pink noise and unison.
	TemporaryDirectory directory;
	vekt::presets::FilePresetRepository repository(directory.path);
	{
		vekt::mono::PluginProcessor author;
		setParameter(author, parameters::performanceMode, 0.0f);
		setParameter(author, parameters::noiseType, 2.0f);
		setParameter(author, parameters::noiseLevel, 40.0f);
		setParameter(author, parameters::unison, 1.0f);
		setParameter(author, parameters::lfos[0].rate, 3.0f);
		setParameter(author, parameters::lfos[0].pitch[0], 2.0f);
		setParameter(author, parameters::lfos[1].rate, 0.7f);
		setParameter(author, parameters::lfos[1].filter, 2.0f);
		setParameter(author, parameters::vibratoRate, 6.0f);
		auto preset = vekt::presets::PresetSchema::create(parameters::presetProductIdentifier, "Reproducible", author.getParameters(), parameters::soundParameterIds);
		preset.identifier = "mono-reproducibility-test";
		preset.soundSchemaVersion = 12;
		REQUIRE(repository.save(preset).wasOk());
	}
	const auto loadAndPlay = [&repository](int warmUpSamples)
	{
		vekt::mono::PluginProcessor processor;
		processor.getPresetSession().library().setUserRepository(&repository);
		processor.prepareToPlay(48'000.0, 512);
		const auto load = [&processor] { REQUIRE(processor.getPresetSession().load("mono-reproducibility-test", vekt::presets::PresetOrigin::user).wasOk()); };
		if (warmUpSamples > 0)
		{
			// Play the same patch first: advances the LFO and vibrato clocks, consumes randomness and moves the
			// pink-noise filter. Then load it again.
			load();
			play(processor, warmUpSamples, 64, true);
		}
		load();
		// Controllers are the player's state and survive a load, so raise the wheel afterwards on both paths.
		juce::MidiBuffer wheel;
		wheel.addEvent(juce::MidiMessage::controllerEvent(1, 1, 127), 0);
		juce::AudioBuffer<float> block(2, 512);
		processor.processBlock(block, wheel);
		return play(processor, 24'064, 57);
	};
	const auto fresh = loadAndPlay(0);
	REQUIRE(fresh.getRMSLevel(0, 0, 24'064) > 0.01f);
	REQUIRE(identical(loadAndPlay(62'976), fresh));
}
