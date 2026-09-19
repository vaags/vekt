#include <vekt/mono/PluginProcessor.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

namespace
{
void setParameter(vekt::mono::PluginProcessor& processor, const char* identifier, float value)
{
	auto* parameter = processor.getParameters().getParameter(identifier);
	REQUIRE(parameter != nullptr);
	parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}
}

TEST_CASE("Mono renders finite stereo MIDI output", "[mono][processor]")
{
	vekt::mono::PluginProcessor processor;
	processor.prepareToPlay(48'000.0, 512);
	juce::AudioBuffer<float> buffer(2, 512);
	juce::MidiBuffer midi;
	midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
	midi.addEvent(juce::MidiMessage::noteOff(1, 60), 384);
	processor.processBlock(buffer, midi);
	float energy {};
	for (int channel = 0; channel < 2; ++channel)
		for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
		{
			REQUIRE(std::isfinite(buffer.getSample(channel, sample)));
			energy += std::abs(buffer.getSample(channel, sample));
		}
	REQUIRE(energy > 0.01f);
}

TEST_CASE("Mono preserves APVTS project state", "[mono][processor]")
{
	vekt::mono::PluginProcessor source;
	setParameter(source, vekt::mono::parameters::filterCutoff, 2'345.0f);
	juce::MemoryBlock state;
	source.getStateInformation(state);
	vekt::mono::PluginProcessor restored;
	restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
	REQUIRE(restored.getParameters().getRawParameterValue(vekt::mono::parameters::filterCutoff)->load() == Catch::Approx(2'345.0f));
}

TEST_CASE("Mono defers voice count while a note is active", "[mono][processor]")
{
	vekt::mono::PluginProcessor processor;
	processor.prepareToPlay(48'000.0, 512);
	juce::AudioBuffer<float> buffer(2, 128);
	juce::MidiBuffer on;
	on.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
	processor.processBlock(buffer, on);
	setParameter(processor, vekt::mono::parameters::voiceCount, 2.0f);
	juce::MidiBuffer empty;
	processor.processBlock(buffer, empty);
	REQUIRE(processor.hasPendingVoiceCountChange());
}

TEST_CASE("Mono provides 24 categorized factory presets", "[mono][processor]")
{
	vekt::mono::PluginProcessor processor;
	auto& session = processor.getPresetSession();
	const auto& catalog = session.library();
	REQUIRE(catalog.factoryPresetCount() == 24);
	REQUIRE(catalog.folders(vekt::presets::PresetOrigin::factory).size() == 6);
	REQUIRE(processor.getNumPrograms() == 24);
	processor.setCurrentProgram(23);
	REQUIRE(processor.getCurrentProgram() == 23);
	REQUIRE(processor.getProgramName(23) == "Transmission FX");
}

TEST_CASE("Mono Legato returns to the last held note", "[mono][processor][midi]")
{
	vekt::mono::PluginProcessor processor;
	setParameter(processor, vekt::mono::parameters::performanceMode, 2.0f);
	setParameter(processor, vekt::mono::parameters::ampRelease, 0.005f);
	processor.prepareToPlay(48'000.0, 512);
	juce::AudioBuffer<float> buffer(2, 512);
	juce::MidiBuffer midi;
	midi.addEvent(juce::MidiMessage::noteOn(1, 48, 0.9f), 0);
	midi.addEvent(juce::MidiMessage::noteOn(1, 72, 0.9f), 128);
	midi.addEvent(juce::MidiMessage::noteOff(1, 72), 256);
	processor.processBlock(buffer, midi);
	float returnedEnergy {};
	for (int sample = 360; sample < buffer.getNumSamples(); ++sample)
		returnedEnergy += std::abs(buffer.getSample(0, sample));
	REQUIRE(returnedEnergy > 0.01f);
}

TEST_CASE("Mono modes isolate held-note stacks by MIDI channel", "[mono][processor][midi]")
{
	vekt::mono::PluginProcessor processor;
	setParameter(processor, vekt::mono::parameters::performanceMode, 1.0f);
	setParameter(processor, vekt::mono::parameters::ampRelease, 0.005f);
	processor.prepareToPlay(48'000.0, 128);
	juce::AudioBuffer<float> buffer(2, 512);
	juce::MidiBuffer midi;
	midi.addEvent(juce::MidiMessage::noteOn(1, 48, 0.9f), 0);
	midi.addEvent(juce::MidiMessage::noteOn(2, 72, 0.9f), 0);
	midi.addEvent(juce::MidiMessage::noteOff(1, 48), 128);
	processor.processBlock(buffer, midi);
	float postReleaseEnergy {};
	for (int sample = 300; sample < buffer.getNumSamples(); ++sample)
		postReleaseEnergy += std::abs(buffer.getSample(0, sample));
	REQUIRE(postReleaseEnergy > 0.01f);
}

TEST_CASE("Mono defers quality changes while transport playback is active", "[mono][processor]")
{
	class PlayHead final : public juce::AudioPlayHead
	{
	public:
		juce::Optional<PositionInfo> getPosition() const override
		{
			PositionInfo position;
			position.setIsPlaying(playing);
			return position;
		}

		bool playing { true };
	} playHead;
	vekt::mono::PluginProcessor processor;
	processor.setPlayHead(&playHead);
	processor.prepareToPlay(48'000.0, 128);
	juce::AudioBuffer<float> buffer(2, 128);
	setParameter(processor, vekt::mono::parameters::quality, 1.0f);
	juce::MidiBuffer empty;
	processor.processBlock(buffer, empty);
	REQUIRE(processor.hasPendingQualityChange());
	playHead.playing = false;
	processor.processBlock(buffer, empty);
	REQUIRE_FALSE(processor.hasPendingQualityChange());
}

TEST_CASE("Mono quality change waits for sustain and release tails after transport stops", "[mono][processor][quality]")
{
	class PlayHead final : public juce::AudioPlayHead
	{
	public:
		juce::Optional<PositionInfo> getPosition() const override
		{
			PositionInfo position;
			position.setIsPlaying(playing);
			return position;
		}

		bool playing { true };
	} playHead;
	vekt::mono::PluginProcessor processor;
	processor.setPlayHead(&playHead);
	setParameter(processor, vekt::mono::parameters::ampRelease, 0.005f);
	processor.prepareToPlay(48'000.0, 128);
	juce::AudioBuffer<float> buffer(2, 128);
	juce::MidiBuffer held;
	held.addEvent(juce::MidiMessage::controllerEvent(1, 64, 127), 0);
	held.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
	held.addEvent(juce::MidiMessage::noteOff(1, 60), 64);
	processor.processBlock(buffer, held);

	setParameter(processor, vekt::mono::parameters::quality, 1.0f);
	playHead.playing = false;
	juce::MidiBuffer empty;
	processor.processBlock(buffer, empty);
	REQUIRE(processor.hasPendingQualityChange());

	juce::MidiBuffer releaseSustain;
	releaseSustain.addEvent(juce::MidiMessage::controllerEvent(1, 64, 0), 0);
	processor.processBlock(buffer, releaseSustain);
	REQUIRE(processor.hasPendingQualityChange());

	for (int block = 0; block < 8 && processor.hasPendingQualityChange(); ++block)
		processor.processBlock(buffer, empty);
	REQUIRE_FALSE(processor.hasPendingQualityChange());
	REQUIRE(processor.getActiveQuality() == 1);
}

TEST_CASE("Mono High quality oversamples synthesis and reports latency", "[mono][processor][quality]")
{
	vekt::mono::PluginProcessor processor;
	setParameter(processor, vekt::mono::parameters::quality, 1.0f);
	processor.prepareToPlay(48'000.0, 512);
	REQUIRE(processor.getLatencySamples() > 0);

	juce::AudioBuffer<float> buffer(2, 512);
	juce::MidiBuffer midi;
	midi.addEvent(juce::MidiMessage::noteOn(1, 96, 0.9f), 0);
	processor.processBlock(buffer, midi);

	float energy {};
	for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
		for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
		{
			REQUIRE(std::isfinite(buffer.getSample(channel, sample)));
			energy += std::abs(buffer.getSample(channel, sample));
		}
	REQUIRE(energy > 0.01f);
}

TEST_CASE("Mono High quality handles multiple note boundaries in one host block", "[mono][processor][quality][midi]")
{
	vekt::mono::PluginProcessor processor;
	setParameter(processor, vekt::mono::parameters::quality, 1.0f);
	setParameter(processor, vekt::mono::parameters::ampRelease, 0.005f);
	processor.prepareToPlay(48'000.0, 512);
	juce::AudioBuffer<float> buffer(2, 512);
	juce::MidiBuffer midi;
	midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
	midi.addEvent(juce::MidiMessage::noteOff(1, 60), 96);
	midi.addEvent(juce::MidiMessage::noteOn(1, 67, 0.9f), 160);
	midi.addEvent(juce::MidiMessage::noteOff(1, 67), 288);
	processor.processBlock(buffer, midi);
	for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
		for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
			REQUIRE(std::isfinite(buffer.getSample(channel, sample)));
}

TEST_CASE("Mono unison spread changes stereo rendering", "[mono][processor][unison]")
{
	vekt::mono::PluginProcessor centered, spread;
	for (auto* processor : { &centered, &spread })
	{
		setParameter(*processor, vekt::mono::parameters::performanceMode, 1.0f);
		setParameter(*processor, vekt::mono::parameters::unison, 2.0f);
		setParameter(*processor, vekt::mono::parameters::unisonDetune, 20.0f);
		setParameter(*processor, vekt::mono::parameters::osc2Level, 0.0f);
		setParameter(*processor, vekt::mono::parameters::osc3Level, 0.0f);
		processor->prepareToPlay(48'000.0, 512);
	}
	setParameter(spread, vekt::mono::parameters::unisonSpread, 100.0f);
	juce::AudioBuffer<float> centeredBuffer(2, 512), spreadBuffer(2, 512);
	juce::MidiBuffer centeredMidi, spreadMidi;
	centeredMidi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
	spreadMidi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
	centered.processBlock(centeredBuffer, centeredMidi);
	spread.processBlock(spreadBuffer, spreadMidi);
	float centeredDifference {}, spreadDifference {};
	for (int sample = 0; sample < centeredBuffer.getNumSamples(); ++sample)
	{
		centeredDifference += std::abs(centeredBuffer.getSample(0, sample) - centeredBuffer.getSample(1, sample));
		spreadDifference += std::abs(spreadBuffer.getSample(0, sample) - spreadBuffer.getSample(1, sample));
	}
	REQUIRE(centeredDifference == Catch::Approx(0.0f).margin(1.0e-6f));
	REQUIRE(spreadDifference > 0.01f);
}

TEST_CASE("Mono rendering is deterministic with drift enabled", "[mono][processor][determinism]")
{
	vekt::mono::PluginProcessor first, second;
	for (auto* processor : { &first, &second })
	{
		setParameter(*processor, vekt::mono::parameters::drift, 100.0f);
		setParameter(*processor, vekt::mono::parameters::osc1Morph, 2.0f);
		setParameter(*processor, vekt::mono::parameters::osc2Level, 0.0f);
		setParameter(*processor, vekt::mono::parameters::osc3Level, 0.0f);
		processor->prepareToPlay(48'000.0, 512);
	}
	juce::AudioBuffer<float> firstBuffer(2, 512), secondBuffer(2, 512);
	juce::MidiBuffer firstMidi, secondMidi;
	firstMidi.addEvent(juce::MidiMessage::noteOn(1, 69, 0.8f), 0);
	secondMidi.addEvent(juce::MidiMessage::noteOn(1, 69, 0.8f), 0);
	first.processBlock(firstBuffer, firstMidi);
	second.processBlock(secondBuffer, secondMidi);
	for (int channel = 0; channel < 2; ++channel)
		for (int sample = 0; sample < 512; ++sample)
			REQUIRE(firstBuffer.getSample(channel, sample) == Catch::Approx(secondBuffer.getSample(channel, sample)).margin(1.0e-7f));
}

TEST_CASE("Mono PolyBLEP oscillator output remains finite across supported rates", "[mono][processor][matrix]")
{
	for (const auto rate : { 44'100.0, 48'000.0, 96'000.0, 192'000.0 })
		for (const auto morph : { 2.0f, 3.0f })
		{
			vekt::mono::PluginProcessor processor;
			setParameter(processor, vekt::mono::parameters::osc1Morph, morph);
			setParameter(processor, vekt::mono::parameters::osc1Level, 100.0f);
			setParameter(processor, vekt::mono::parameters::osc2Level, 0.0f);
			setParameter(processor, vekt::mono::parameters::osc3Level, 0.0f);
			processor.prepareToPlay(rate, 512);
			juce::AudioBuffer<float> buffer(2, 512);
			juce::MidiBuffer midi;
			midi.addEvent(juce::MidiMessage::noteOn(1, 120, 0.9f), 0);
			processor.processBlock(buffer, midi);
			for (int channel = 0; channel < 2; ++channel)
				for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
					REQUIRE(std::isfinite(buffer.getSample(channel, sample)));
		}
}

TEST_CASE("Mono handles duplicate notes and channel panic messages without stuck output", "[mono][processor][midi]")
{
	vekt::mono::PluginProcessor processor;
	setParameter(processor, vekt::mono::parameters::performanceMode, 1.0f);
	setParameter(processor, vekt::mono::parameters::ampRelease, 0.005f);
	processor.prepareToPlay(48'000.0, 128);
	juce::AudioBuffer<float> buffer(2, 128);
	juce::MidiBuffer midi;
	midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
	midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 8);
	midi.addEvent(juce::MidiMessage::noteOff(1, 60), 16);
	midi.addEvent(juce::MidiMessage::allNotesOff(1), 32);
	midi.addEvent(juce::MidiMessage::allSoundOff(1), 64);
	processor.processBlock(buffer, midi);
	float tailEnergy {};
	for (int sample = 96; sample < buffer.getNumSamples(); ++sample)
		tailEnergy += std::abs(buffer.getSample(0, sample)) + std::abs(buffer.getSample(1, sample));
	REQUIRE(tailEnergy == Catch::Approx(0.0f).margin(1.0e-7f));
}