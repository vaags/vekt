#include <vekt/mono/PluginProcessor.h>

#include "Lfo.h"

#include <vekt/presets/PresetSchema.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>
#include <vector>

namespace
{
namespace parameters = vekt::mono::parameters;

void setParameter(vekt::mono::PluginProcessor& processor, const char* identifier, float value)
{
	auto* parameter = processor.getParameters().getParameter(identifier);
	REQUIRE(parameter != nullptr);
	parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

float rawValue(vekt::mono::PluginProcessor& processor, const char* identifier)
{
	return processor.getParameters().getRawParameterValue(identifier)->load();
}

// The parameter's own default, independent of the factory preset a new processor loads.
float defaultValue(vekt::mono::PluginProcessor& processor, const char* identifier)
{
	const auto* parameter = processor.getParameters().getParameter(identifier);
	return parameter->convertFrom0to1(parameter->getDefaultValue());
}

// A patch where every LFO destination is audible: all three oscillators on pulse
// (so width matters), noise, two-voice unison, drive and a partly closed filter.
void initializeRichPatch(vekt::mono::PluginProcessor& processor)
{
	for (auto* parameter : processor.juce::AudioProcessor::getParameters())
		parameter->setValueNotifyingHost(parameter->getDefaultValue());
	for (const auto* level : { parameters::osc1Level, parameters::osc2Level, parameters::osc3Level }) setParameter(processor, level, 50.0f);
	for (const auto* morph : { parameters::osc1Morph, parameters::osc2Morph, parameters::osc3Morph }) setParameter(processor, morph, 3.0f);
	setParameter(processor, parameters::noiseType, 1.0f);
	setParameter(processor, parameters::noiseLevel, 30.0f);
	setParameter(processor, parameters::unison, 1.0f);
	setParameter(processor, parameters::unisonDetune, 10.0f);
	setParameter(processor, parameters::unisonSpread, 50.0f);
	setParameter(processor, parameters::filterDrive, 6.0f);
	setParameter(processor, parameters::filterCutoff, 1'500.0f);
	setParameter(processor, parameters::ampSustain, 100.0f);
}

juce::AudioBuffer<float> renderNote(vekt::mono::PluginProcessor& processor, int samples, int blockSize = 512, int note = 57)
{
	processor.prepareToPlay(48'000.0, blockSize);
	juce::AudioBuffer<float> output(2, samples);
	juce::AudioBuffer<float> block(2, blockSize);
	for (int start = 0; start < samples; start += blockSize)
	{
		const auto count = std::min(blockSize, samples - start);
		juce::MidiBuffer midi;
		if (start == 0) midi.addEvent(juce::MidiMessage::noteOn(1, note, 0.8f), 0);
		block.setSize(2, count, false, false, true);
		block.clear();
		processor.processBlock(block, midi);
		for (int channel = 0; channel < 2; ++channel) output.copyFrom(channel, start, block, channel, 0, count);
	}
	return output;
}

float differenceRms(const juce::AudioBuffer<float>& first, const juce::AudioBuffer<float>& second)
{
	double sum {};
	for (int channel = 0; channel < 2; ++channel)
		for (int sample = 0; sample < first.getNumSamples(); ++sample)
		{
			const auto difference = static_cast<double>(first.getSample(channel, sample)) - second.getSample(channel, sample);
			sum += difference * difference;
		}
	return static_cast<float>(std::sqrt(sum / (2.0 * first.getNumSamples())));
}

int zeroCrossings(const juce::AudioBuffer<float>& buffer, int start, int end)
{
	int crossings {};
	for (int sample = start + 1; sample < end; ++sample)
		if ((buffer.getSample(0, sample - 1) < 0.0f) != (buffer.getSample(0, sample) < 0.0f)) ++crossings;
	return crossings;
}

// One sine oscillator through an open filter, so pitch can be read from zero crossings.
void initializeSine(vekt::mono::PluginProcessor& processor)
{
	for (auto* parameter : processor.juce::AudioProcessor::getParameters())
		parameter->setValueNotifyingHost(parameter->getDefaultValue());
	setParameter(processor, parameters::osc1Morph, 0.0f);
	setParameter(processor, parameters::osc1Level, 50.0f);
	setParameter(processor, parameters::filterCutoff, 20'000.0f);
	setParameter(processor, parameters::filterResonance, 0.0f);
	setParameter(processor, parameters::filterKeyTracking, 0.0f);
	setParameter(processor, parameters::filterEnvelopeAmount, 0.0f);
	setParameter(processor, parameters::ampAttack, 0.0005f);
	setParameter(processor, parameters::ampSustain, 100.0f);
	// A unipolar square at the slowest rate holds a constant 1 for the first 50 seconds of each note.
	setParameter(processor, parameters::lfos[0].rate, 0.01f);
	setParameter(processor, parameters::lfos[0].shape, 4.0f);
	setParameter(processor, parameters::lfos[0].polarity, 1.0f);
	setParameter(processor, parameters::lfos[0].mode, 1.0f);
}

// A playing host transport that moves on by one 512-sample block at 48 kHz per query.
// Renders one block per 512 samples; events are delivered in the first block at the given sample positions.
juce::AudioBuffer<float> renderEvents(vekt::mono::PluginProcessor& processor, int samples,
	const std::vector<std::pair<juce::MidiMessage, int>>& events)
{
	constexpr int blockSize = 512;
	processor.prepareToPlay(48'000.0, blockSize);
	juce::AudioBuffer<float> output(2, samples);
	juce::AudioBuffer<float> block(2, blockSize);
	for (int start = 0; start < samples; start += blockSize)
	{
		const auto count = std::min(blockSize, samples - start);
		juce::MidiBuffer midi;
		if (start == 0)
			for (const auto& [message, position] : events) midi.addEvent(message, position);
		block.setSize(2, count, false, false, true);
		block.clear();
		processor.processBlock(block, midi);
		for (int channel = 0; channel < 2; ++channel) output.copyFrom(channel, start, block, channel, 0, count);
	}
	return output;
}

bool identical(const juce::AudioBuffer<float>& first, const juce::AudioBuffer<float>& second)
{
	for (int channel = 0; channel < 2; ++channel)
		for (int sample = 0; sample < first.getNumSamples(); ++sample)
			if (first.getSample(channel, sample) != second.getSample(channel, sample)) return false;
	return true;
}

class TransportPlayHead final : public juce::AudioPlayHead
{
public:
	juce::Optional<PositionInfo> getPosition() const override
	{
		PositionInfo position;
		position.setIsPlaying(true);
		position.setBpm(bpm);
		position.setPpqPosition(ppq);
		ppq += 512.0 / 48'000.0 * bpm / 60.0;
		return position;
	}
	double bpm { 120.0 };
	mutable double ppq {};
};
}

TEST_CASE("Mono LFO parameters are sound parameters that default to no modulation", "[mono][lfo][parameters]")
{
	vekt::mono::PluginProcessor processor;
	REQUIRE(parameters::soundParameterIds.size() == parameters::legacySoundParameterIds.size() + 56 + parameters::vibratoParameterIds.size()
		+ parameters::schema8ParameterIds.size() + parameters::schema10ParameterIds.size() + parameters::schema12ParameterIds.size());
	for (const auto& lfo : parameters::lfos)
	{
		const auto ids = lfo.all();
		for (std::size_t index = 0; index < ids.size(); ++index)
		{
			INFO(ids[index]);
			REQUIRE(processor.getParameters().getParameter(ids[index]) != nullptr);
			REQUIRE(std::find(parameters::soundParameterIds.begin(), parameters::soundParameterIds.end(), ids[index]) != parameters::soundParameterIds.end());
			// The first ten are source controls; the rest are destination depths.
			if (index >= 10) REQUIRE(defaultValue(processor, ids[index]) == 0.0f);
		}
		REQUIRE(processor.getParameters().getParameter(lfo.filterMode) != nullptr);
		REQUIRE(std::find(parameters::soundParameterIds.begin(), parameters::soundParameterIds.end(), lfo.filterMode) != parameters::soundParameterIds.end());
		REQUIRE(defaultValue(processor, lfo.filterMode) == 0.0f);
		REQUIRE(defaultValue(processor, lfo.amount) == Catch::Approx(100.0f));
		REQUIRE(defaultValue(processor, lfo.rate) == Catch::Approx(2.0f));
		auto* rate = dynamic_cast<juce::RangedAudioParameter*>(processor.getParameters().getParameter(lfo.rate));
		REQUIRE(rate->getNormalisableRange().start == Catch::Approx(vekt::mono::minimumLfoRateHz));
		REQUIRE(rate->getNormalisableRange().end == Catch::Approx(vekt::mono::maximumLfoRateHz));
	}
}

TEST_CASE("Mono LFO source settings leave the sound unchanged at zero depth", "[mono][lfo]")
{
	vekt::mono::PluginProcessor reference, configured;
	initializeRichPatch(reference);
	initializeRichPatch(configured);
	for (const auto& lfo : parameters::lfos)
	{
		setParameter(configured, lfo.rate, 37.0f);
		setParameter(configured, lfo.shape, 4.0f);
		setParameter(configured, lfo.polarity, 1.0f);
		setParameter(configured, lfo.mode, 2.0f);
		setParameter(configured, lfo.phase, 90.0f);
		setParameter(configured, lfo.fade, 0.05f);
		setParameter(configured, lfo.amount, 50.0f);
	}
	const auto expected = renderNote(reference, 12'000);
	const auto actual = renderNote(configured, 12'000);
	for (int channel = 0; channel < 2; ++channel)
		for (int sample = 0; sample < expected.getNumSamples(); ++sample)
			REQUIRE(actual.getSample(channel, sample) == expected.getSample(channel, sample));
}

TEST_CASE("Mono LFOs reach every destination", "[mono][lfo][slow]")
{
	vekt::mono::PluginProcessor reference;
	initializeRichPatch(reference);
	const auto expected = renderNote(reference, 9'600);
	for (std::size_t lfoIndex = 0; lfoIndex < parameters::lfos.size(); ++lfoIndex)
	{
		const auto ids = parameters::lfos[lfoIndex].depths();
		for (std::size_t index = 0; index < ids.size(); ++index)
		{
			INFO("LFO " << lfoIndex + 1 << " destination " << ids[index]);
			vekt::mono::PluginProcessor modulated;
			initializeRichPatch(modulated);
			setParameter(modulated, parameters::lfos[lfoIndex].rate, 8.0f);
			auto* depth = dynamic_cast<juce::RangedAudioParameter*>(modulated.getParameters().getParameter(ids[index]));
			// Amp only attenuates, so drive it negative; the rest use their full positive depth.
			const auto isAmp = juce::String(ids[index]).endsWith("Amp");
			setParameter(modulated, ids[index], isAmp ? depth->getNormalisableRange().start : depth->getNormalisableRange().end);
			REQUIRE(differenceRms(renderNote(modulated, 9'600), expected) > 1.0e-3f);
		}
	}
}

TEST_CASE("Mono LFO pitch depth is in semitones and scaled by Amount", "[mono][lfo]")
{
	for (const auto [amount, expectedHz] : { std::pair { 100.0f, 880.0f }, std::pair { 50.0f, 440.0f * std::sqrt(2.0f) } })
	{
		CAPTURE(amount);
		vekt::mono::PluginProcessor processor;
		initializeSine(processor);
		setParameter(processor, parameters::lfos[0].pitch[0], 12.0f);
		setParameter(processor, parameters::lfos[0].amount, amount);
		const auto output = renderNote(processor, 24'480, 512, 69);
		// Measure half a second after the 1 ms LFO smoothing and envelope attack have settled.
		const auto measuredHz = zeroCrossings(output, 480, 24'480) / 2.0f / 0.5f;
		REQUIRE(measuredHz == Catch::Approx(expectedHz).margin(3.0f));
	}
}

TEST_CASE("Mono LFO amp depth attenuates without boosting", "[mono][lfo]")
{
	vekt::mono::PluginProcessor full, silenced, boosted;
	for (auto* processor : { &full, &silenced, &boosted }) initializeSine(*processor);
	setParameter(silenced, parameters::lfos[0].amp, -100.0f);
	setParameter(boosted, parameters::lfos[0].amp, 100.0f);
	const auto reference = renderNote(full, 9'600);
	const auto quiet = renderNote(silenced, 9'600);
	const auto unchanged = renderNote(boosted, 9'600);
	REQUIRE(reference.getMagnitude(480, 9'120) > 0.05f);
	REQUIRE(quiet.getMagnitude(480, 9'120) < 1.0e-4f);
	REQUIRE(differenceRms(unchanged, reference) == 0.0f);
}

TEST_CASE("Mono LFO output is independent of block size", "[mono][lfo]")
{
	vekt::mono::PluginProcessor small, large;
	for (auto* processor : { &small, &large })
	{
		initializeRichPatch(*processor);
		setParameter(*processor, parameters::lfos[0].rate, 6.0f);
		setParameter(*processor, parameters::lfos[0].filter, 2.0f);
		setParameter(*processor, parameters::lfos[0].pitch[1], 0.5f);
		setParameter(*processor, parameters::lfos[1].rate, 0.7f);
		setParameter(*processor, parameters::lfos[1].shape, 5.0f);
		setParameter(*processor, parameters::lfos[1].mode, 1.0f);
		setParameter(*processor, parameters::lfos[1].spread, -60.0f);
	}
	const auto first = renderNote(small, 12'000, 64);
	const auto second = renderNote(large, 12'000, 1'000);
	REQUIRE(differenceRms(first, second) < 1.0e-6f);
}

TEST_CASE("Mono synced free LFO follows the host song position", "[mono][lfo]")
{
	REQUIRE(vekt::mono::syncedLfoRateHz(120.0, vekt::mono::defaultLfoDivision) == Catch::Approx(2.0f));
	REQUIRE(vekt::mono::syncedLfoRateHz(120.0, 0) == Catch::Approx(0.5f));
	REQUIRE(vekt::mono::syncedLfoRateHz(120.0, 5) == Catch::Approx(1.5f));        // 1/2 T lasts 4/3 beats
	REQUIRE(vekt::mono::syncedLfoRateHz(120.0, 7) == Catch::Approx(2.0f / 1.5f)); // 1/4 D lasts 1.5 beats
	REQUIRE(vekt::mono::syncedLfoRateHz(90.0, 12) == Catch::Approx(6.0f));

	// At 1/4 sync one LFO cycle is one beat: starting a beat later sounds identical, half a beat later does not.
	const auto renderAt = [](double ppq)
	{
		TransportPlayHead playHead;
		playHead.ppq = ppq;
		vekt::mono::PluginProcessor processor;
		initializeSine(processor);
		setParameter(processor, parameters::lfos[0].mode, 0.0f);
		setParameter(processor, parameters::lfos[0].polarity, 0.0f);
		setParameter(processor, parameters::lfos[0].shape, 0.0f);
		setParameter(processor, parameters::lfos[0].sync, 1.0f);
		setParameter(processor, parameters::lfos[0].pitch[0], 2.0f);
		processor.setPlayHead(&playHead);
		return renderNote(processor, 4'800);
	};
	const auto onBeat = renderAt(8.0);
	REQUIRE(differenceRms(renderAt(9.0), onBeat) < 1.0e-6f);
	REQUIRE(differenceRms(renderAt(8.5), onBeat) > 1.0e-3f);
}

TEST_CASE("Mono migrates earlier presets to the current schema with the LFOs at their defaults", "[mono][lfo][preset]")
{
	vekt::mono::PluginProcessor processor;
	vekt::presets::Preset factory;
	REQUIRE(processor.getPresetSession().library().loadFactoryPreset(0, factory).wasOk());
	auto schema6 = vekt::presets::PresetSchema::create(parameters::presetProductIdentifier, "Schema 6",
		processor.getParameters(), parameters::legacySoundParameterIds);
	schema6.soundSchemaVersion = 6;
	for (auto preset : { factory, schema6 })
	{
		CAPTURE(preset.soundSchemaVersion);
		setParameter(processor, parameters::lfos[1].filter, 3.0f);
		setParameter(processor, parameters::lfos[0].amount, 20.0f);
		REQUIRE(processor.getPresetSession().prepare(preset).wasOk());
		REQUIRE(preset.soundSchemaVersion == 12);
		REQUIRE(preset.parameters.size() == parameters::soundParameterIds.size());
		REQUIRE(vekt::presets::PresetSchema::apply(preset, parameters::presetProductIdentifier,
			processor.getParameters(), parameters::soundParameterIds).wasOk());
		REQUIRE(rawValue(processor, parameters::lfos[1].filter) == 0.0f);
		REQUIRE(rawValue(processor, parameters::lfos[0].amount) == Catch::Approx(100.0f));
	}
}

TEST_CASE("Mono LFO settings recall with project state and reset for older projects", "[mono][lfo][state]")
{
	vekt::mono::PluginProcessor source;
	setParameter(source, parameters::lfos[0].pitch[2], 7.0f);
	setParameter(source, parameters::lfos[1].mode, 2.0f);
	juce::MemoryBlock state;
	source.getStateInformation(state);

	vekt::mono::PluginProcessor restored;
	restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
	REQUIRE(rawValue(restored, parameters::lfos[0].pitch[2]) == Catch::Approx(7.0f));
	REQUIRE(rawValue(restored, parameters::lfos[1].mode) == Catch::Approx(2.0f));

	// A project saved before the LFOs existed must not inherit the live LFO settings.
	auto tree = juce::ValueTree::readFromData(state.getData(), state.getSize());
	std::function<void(juce::ValueTree)> removeLfoParameters = [&](juce::ValueTree node)
	{
		for (int index = node.getNumChildren(); --index >= 0;)
		{
			auto child = node.getChild(index);
			if (child.getProperty("id").toString().startsWith("lfo")) node.removeChild(index, nullptr);
			else removeLfoParameters(child);
		}
	};
	removeLfoParameters(tree);
	juce::MemoryOutputStream legacy;
	tree.writeToStream(legacy);
	vekt::mono::PluginProcessor live;
	setParameter(live, parameters::lfos[0].pitch[2], -5.0f);
	setParameter(live, parameters::lfos[0].amount, 10.0f);
	live.setStateInformation(legacy.getData(), static_cast<int>(legacy.getDataSize()));
	REQUIRE(rawValue(live, parameters::lfos[0].pitch[2]) == 0.0f);
	REQUIRE(rawValue(live, parameters::lfos[0].amount) == Catch::Approx(100.0f));
}

TEST_CASE("Mono vibrato is silent until the mod wheel or aftertouch is used", "[mono][vibrato]")
{
	vekt::mono::PluginProcessor withoutDepth, withDepth;
	for (auto* processor : { &withoutDepth, &withDepth }) initializeSine(*processor);
	setParameter(withoutDepth, parameters::vibratoDepth, 0.0f);
	setParameter(withDepth, parameters::vibratoDepth, 100.0f);
	const std::vector events { std::pair { juce::MidiMessage::noteOn(1, 69, 0.8f), 0 } };
	REQUIRE(rawValue(withDepth, parameters::vibratoDepth) == Catch::Approx(100.0f));
	REQUIRE(identical(renderEvents(withoutDepth, 9'600, events), renderEvents(withDepth, 9'600, events)));
}

TEST_CASE("Mono vibrato reaches its depth in cents with the mod wheel up", "[mono][vibrato]")
{
	vekt::mono::PluginProcessor processor;
	initializeSine(processor);
	setParameter(processor, parameters::vibratoRate, 0.1f);
	setParameter(processor, parameters::vibratoDepth, 100.0f);
	const auto output = renderEvents(processor, 134'400, {
		{ juce::MidiMessage::controllerEvent(1, 1, 127), 0 }, { juce::MidiMessage::noteOn(1, 69, 0.8f), 0 } });
	// Between 2.2 s and 2.8 s a 0.1 Hz sine is within 2% of its peak: about +99 cents above A440.
	const auto measuredHz = zeroCrossings(output, 105'600, 134'400) / 2.0f / 0.6f;
	REQUIRE(measuredHz == Catch::Approx(440.0f * std::exp2(1.0f / 12.0f)).margin(2.0f));
	REQUIRE(processor.getVibratoControlDisplay() == Catch::Approx(1.0f));
}

TEST_CASE("Mono vibrato responds equally to mod wheel, channel pressure and poly aftertouch", "[mono][vibrato][midi]")
{
	const auto render = [](const juce::MidiMessage& control)
	{
		vekt::mono::PluginProcessor processor;
		initializeSine(processor);
		setParameter(processor, parameters::vibratoRate, 7.0f);
		setParameter(processor, parameters::vibratoDepth, 100.0f);
		return renderEvents(processor, 14'400, { { juce::MidiMessage::noteOn(1, 69, 0.8f), 0 }, { control, 1 } });
	};
	const auto wheel = render(juce::MidiMessage::controllerEvent(1, 1, 127));
	REQUIRE(identical(wheel, render(juce::MidiMessage::channelPressureChange(1, 127))));
	REQUIRE(identical(wheel, render(juce::MidiMessage::aftertouchChange(1, 69, 127))));
	// Controllers only reach notes on their own channel and key.
	const auto dry = render(juce::MidiMessage::controllerEvent(1, 7, 100));
	REQUIRE_FALSE(identical(wheel, dry));
	REQUIRE(identical(dry, render(juce::MidiMessage::controllerEvent(2, 1, 127))));
	REQUIRE(identical(dry, render(juce::MidiMessage::aftertouchChange(1, 70, 127))));
}

TEST_CASE("Mono reset all controllers returns the vibrato controls to rest", "[mono][vibrato][midi]")
{
	vekt::mono::PluginProcessor processor;
	initializeSine(processor);
	renderEvents(processor, 512, { { juce::MidiMessage::controllerEvent(3, 1, 64), 0 }, { juce::MidiMessage::channelPressureChange(3, 100), 0 } });
	REQUIRE(processor.getVibratoControlDisplay() == Catch::Approx(100.0f / 127.0f));
	juce::AudioBuffer<float> block(2, 512);
	juce::MidiBuffer reset;
	reset.addEvent(juce::MidiMessage::controllerEvent(3, 121, 0), 0);
	processor.processBlock(block, reset);
	REQUIRE(processor.getVibratoControlDisplay() == 0.0f);
}
