#include <vekt/mono/PluginProcessor.h>

#include "MonoVoice.h"
#include "ContourEnvelope.h"
#include "MonoRenderWorkers.h"

#include <vekt/audio_analysis/Measurements.h>
#include <vekt/presets/PresetSchema.h>

#include <juce_events/juce_events.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <memory>
#include <set>
#include <thread>

namespace
{
void setParameter(vekt::mono::PluginProcessor& processor, const char* identifier, float value)
{
	auto* parameter = processor.getParameters().getParameter(identifier);
	REQUIRE(parameter != nullptr);
	parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

float rms(const juce::AudioBuffer<float>& buffer)
{
	const std::span samples(buffer.getReadPointer(0), static_cast<std::size_t>(buffer.getNumSamples()));
	return static_cast<float>(vekt::audio_analysis::measureSamples<float>(samples).rms);
}

float stereoRms(const juce::AudioBuffer<float>& buffer)
{
	double sum {};
	for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
		for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
		{
			const auto value = buffer.getSample(channel, sample);
			sum += static_cast<double>(value) * value;
		}
	return static_cast<float>(std::sqrt(sum / static_cast<double>(buffer.getNumChannels() * buffer.getNumSamples())));
}

// Root-sum-square of channel RMS values, not an arithmetic waveform sum and
// not the per-channel average used by stereoRms. Preserves equal-power pan.
float stereoPowerRms(const juce::AudioBuffer<float>& buffer)
{
	double sum {};
	for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
		for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
		{
			const auto value = buffer.getSample(channel, sample);
			sum += static_cast<double>(value) * value;
		}
	return static_cast<float>(std::sqrt(sum / static_cast<double>(buffer.getNumSamples())));
}

float differenceRms(const juce::AudioBuffer<float>& buffer)
{
	const std::span samples(buffer.getReadPointer(0), static_cast<std::size_t>(buffer.getNumSamples()));
	return static_cast<float>(vekt::audio_analysis::measureSamples<float>(samples).differenceRms);
}

float sinusoidMagnitude(const juce::AudioBuffer<float>& buffer, float frequency, float sampleRate)
{
	vekt::audio_analysis::SinusoidalProjector projector(sampleRate, frequency);
	for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
		projector.add(buffer.getSample(0, sample));
	return static_cast<float>(projector.peakAmplitude());
}

std::pair<float, float> dominantFrequency(const juce::AudioBuffer<float>& buffer, float startFrequency,
	float endFrequency, float sampleRate)
{
	float strongestFrequency = startFrequency;
	float strongestMagnitude {};
	for (auto frequency = startFrequency; frequency <= endFrequency; frequency += 5.0f)
		if (const auto magnitude = sinusoidMagnitude(buffer, frequency, sampleRate); magnitude > strongestMagnitude)
		{
			strongestFrequency = frequency;
			strongestMagnitude = magnitude;
		}
	return { strongestFrequency, strongestMagnitude };
}

void renderBlock(vekt::mono::PluginProcessor& processor, juce::AudioBuffer<float>& buffer, juce::MidiBuffer midi = {})
{
	buffer.clear();
	processor.processBlock(buffer, midi);
}

void initializeDryVoice(vekt::mono::PluginProcessor& processor)
{
	for (auto* parameter : processor.juce::AudioProcessor::getParameters())
		parameter->setValueNotifyingHost(parameter->getDefaultValue());
	setParameter(processor, vekt::mono::parameters::performanceMode, 1.0f);
	setParameter(processor, vekt::mono::parameters::osc1Morph, 0.0f);
	setParameter(processor, vekt::mono::parameters::osc1Level, 20.0f);
	setParameter(processor, vekt::mono::parameters::filterCutoff, 20'000.0f);
	setParameter(processor, vekt::mono::parameters::filterResonance, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterKeyTracking, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterEnvelopeAmount, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterVelocity, 0.0f);
	setParameter(processor, vekt::mono::parameters::ampAttack, 0.0005f);
	setParameter(processor, vekt::mono::parameters::ampSustain, 100.0f);
	setParameter(processor, vekt::mono::parameters::ampVelocity, 0.0f);
}
}

TEST_CASE("Mono stereo power RMS preserves mono power across equal-power pan and phase", "[mono][processor][stereo]")
{
	juce::AudioBuffer<float> buffer(2, 4);
	const std::array mono { 0.4f, -0.4f, 0.2f, -0.2f };
	const auto sourceRms = std::sqrt((0.4f * 0.4f + 0.4f * 0.4f
		+ 0.2f * 0.2f + 0.2f * 0.2f) / 4.0f);
	for (const auto pan : { -1.0f, 0.0f, 1.0f })
	{
		const auto left = std::sqrt(0.5f * (1.0f - pan));
		const auto right = std::sqrt(0.5f * (1.0f + pan));
		for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
		{
			buffer.setSample(0, sample, mono[static_cast<std::size_t>(sample)] * left);
			buffer.setSample(1, sample, mono[static_cast<std::size_t>(sample)] * right);
		}
		CAPTURE(pan);
		REQUIRE(stereoPowerRms(buffer) == Catch::Approx(sourceRms).margin(1.0e-7f));
		REQUIRE(stereoRms(buffer) == Catch::Approx(sourceRms / std::sqrt(2.0f)).margin(1.0e-7f));
	}
	// Root-sum-square power must not accidentally turn into a waveform sum:
	// opposite-polarity channels cancel in L+R, but not in channel power.
	for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
	{
		const auto value = mono[static_cast<std::size_t>(sample)] / std::sqrt(2.0f);
		buffer.setSample(0, sample, value);
		buffer.setSample(1, sample, -value);
		REQUIRE(std::abs(buffer.getSample(0, sample) + buffer.getSample(1, sample)) < 1.0e-7f);
	}
	REQUIRE(stereoPowerRms(buffer) == Catch::Approx(sourceRms).margin(1.0e-7f));
}

TEST_CASE("Mono oscillator ranges follow footage labels", "[mono][processor][oscillator]")
{
	juce::ScopedJuceInitialiser_GUI juceInitializer;
	const std::array ranges { vekt::mono::parameters::osc1Range, vekt::mono::parameters::osc2Range, vekt::mono::parameters::osc3Range };
	const std::array levels { vekt::mono::parameters::osc1Level, vekt::mono::parameters::osc2Level, vekt::mono::parameters::osc3Level };
	const std::array morphs { vekt::mono::parameters::osc1Morph, vekt::mono::parameters::osc2Morph, vekt::mono::parameters::osc3Morph };
	for (std::size_t oscillator = 0; oscillator < ranges.size(); ++oscillator)
		for (int range = 0; range < 5; ++range)
		{
			vekt::mono::PluginProcessor processor;
			initializeDryVoice(processor);
			setParameter(processor, vekt::mono::parameters::osc1Level, 0.0f);
			setParameter(processor, levels[oscillator], 20.0f);
			setParameter(processor, morphs[oscillator], 0.0f);
			setParameter(processor, ranges[oscillator], static_cast<float>(range));
			processor.prepareToPlay(48'000.0, 4800);
			juce::AudioBuffer<float> buffer(2, 4800);
			juce::MidiBuffer midi;
			midi.addEvent(juce::MidiMessage::noteOn(1, 69, 1.0f), 0);
			renderBlock(processor, buffer, midi);
			renderBlock(processor, buffer);
			const auto expected = 440.0f * std::exp2(static_cast<float>(range - 1));
			INFO("oscillator=" << oscillator + 1 << ", range=" << range << ", expected=" << expected);
			CHECK(sinusoidMagnitude(buffer, expected, 48'000.0f) > rms(buffer));
		}
}

TEST_CASE("Mono input Q compensation is inert at zero resonance and changes driven sound", "[mono][processor][filter][qcomp][slow]")
{
	juce::ScopedJuceInitialiser_GUI juceInitializer;
	for (const auto sampleRate : { 44'100.0, 48'000.0, 96'000.0 })
		for (const auto quality : { 0.0f, 1.0f })
			for (const auto emphasis : { 0.0f, 50.0f, 100.0f })
				for (const auto drive : { 0.0f, 24.0f })
				{
					vekt::mono::PluginProcessor dry, compensated;
					for (auto* processor : { &dry, &compensated })
					{
						initializeDryVoice(*processor);
						setParameter(*processor, vekt::mono::parameters::quality, quality);
						setParameter(*processor, vekt::mono::parameters::osc1Level, 100.0f);
						setParameter(*processor, vekt::mono::parameters::filterCutoff, 1'000.0f);
						setParameter(*processor, vekt::mono::parameters::filterResonance, emphasis);
						setParameter(*processor, vekt::mono::parameters::filterDrive, drive);
					}
					setParameter(compensated, vekt::mono::parameters::filterQCompensation, 1.0f);
					dry.prepareToPlay(sampleRate, 1024);
					compensated.prepareToPlay(sampleRate, 1024);
					juce::AudioBuffer<float> dryBuffer(2, 1024), wetBuffer(2, 1024);
					juce::MidiBuffer midi;
					midi.addEvent(juce::MidiMessage::noteOn(1, 48, 1.0f), 0);
					renderBlock(dry, dryBuffer, midi);
					renderBlock(compensated, wetBuffer, midi);
					for (int block = 0; block < 32; ++block)
					{
						renderBlock(dry, dryBuffer);
						renderBlock(compensated, wetBuffer);
					}
					INFO("rate=" << sampleRate << ", quality=" << quality << ", emphasis=" << emphasis << ", drive=" << drive);
					REQUIRE(rms(dryBuffer) > 0.0001f);
					REQUIRE(std::isfinite(rms(wetBuffer)));
					REQUIRE(dry.getLatencySamples() == compensated.getLatencySamples());
					if (emphasis == 0.0f)
						for (int sample = 0; sample < dryBuffer.getNumSamples(); ++sample)
							REQUIRE(std::bit_cast<std::uint32_t>(wetBuffer.getSample(0, sample))
								== std::bit_cast<std::uint32_t>(dryBuffer.getSample(0, sample)));
					else if (drive == 0.0f)
						REQUIRE(std::abs(rms(wetBuffer) - rms(dryBuffer)) > 0.0001f);
					REQUIRE(compensated.coupledWorkSnapshot().unconverged == 0);
					REQUIRE(compensated.coupledWorkSnapshot().nonFinite == 0);
				}
}

TEST_CASE("Mono input Q compensation switches smoothly on a held voice", "[mono][processor][filter][qcomp]")
{
	juce::ScopedJuceInitialiser_GUI juceInitializer;
	vekt::mono::PluginProcessor dry, switched;
	for (auto* processor : { &dry, &switched })
	{
		initializeDryVoice(*processor);
		setParameter(*processor, vekt::mono::parameters::filterCutoff, 1'000.0f);
		setParameter(*processor, vekt::mono::parameters::filterResonance, 50.0f);
		setParameter(*processor, vekt::mono::parameters::filterDrive, 24.0f);
		processor->prepareToPlay(48'000.0, 2400);
	}
	juce::AudioBuffer<float> dryBuffer(2, 2400), switchedBuffer(2, 2400);
	juce::MidiBuffer midi;
	midi.addEvent(juce::MidiMessage::noteOn(1, 48, 1.0f), 0);
	renderBlock(dry, dryBuffer, midi);
	renderBlock(switched, switchedBuffer, midi);
	for (const auto enabled : { true, false })
	{
		setParameter(switched, vekt::mono::parameters::filterQCompensation, enabled ? 1.0f : 0.0f);
		renderBlock(dry, dryBuffer);
		renderBlock(switched, switchedBuffer);
		REQUIRE(switched.coupledWorkSnapshot().unconverged == 0);
		REQUIRE(switched.coupledWorkSnapshot().nonFinite == 0);
		for (int sample = 0; sample < 2400; ++sample)
			REQUIRE(std::isfinite(switchedBuffer.getSample(0, sample)));
		REQUIRE(std::abs(switchedBuffer.getSample(0, 0) - dryBuffer.getSample(0, 0)) < 0.5f);
	}
}

TEST_CASE("Mono Q compensation defaults off and recalls sound state", "[mono][processor][state][qcomp]")
{
	juce::ScopedJuceInitialiser_GUI juceInitializer;
	vekt::mono::PluginProcessor processor;
	auto* parameter = processor.getParameters().getParameter(vekt::mono::parameters::filterQCompensation);
	REQUIRE(parameter != nullptr);
	REQUIRE(parameter->getDefaultValue() == Catch::Approx(0.0f));
	REQUIRE(parameter->getValue() == Catch::Approx(0.0f));
	for (const auto savedValue : { 1.0f, 0.0f })
	{
		setParameter(processor, vekt::mono::parameters::filterQCompensation, savedValue);
		juce::MemoryBlock state;
		processor.getStateInformation(state);
		parameter->setValueNotifyingHost(savedValue > 0.5f ? 0.72f : 0.28f);
		processor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
		REQUIRE(parameter->getValue() == Catch::Approx(savedValue));
	}
	setParameter(processor, vekt::mono::parameters::filterQCompensation, 1.0f);
	for (int index = 0; index < processor.getNumPrograms(); ++index)
	{
		processor.setCurrentProgram(index);
		REQUIRE(parameter->getValue() == Catch::Approx(0.0f));
		setParameter(processor, vekt::mono::parameters::filterQCompensation, 1.0f);
	}
}

TEST_CASE("Mono filter Mode defaults to LP, names its landmarks and recalls with state", "[mono][processor][state][ladder-mode]")
{
	juce::ScopedJuceInitialiser_GUI juceInitializer;
	vekt::mono::PluginProcessor processor;
	auto* parameter = processor.getParameters().getParameter(vekt::mono::parameters::filterMode);
	REQUIRE(parameter != nullptr);
	REQUIRE(parameter->getDefaultValue() == Catch::Approx(0.0f));
	for (const auto [value, text] : { std::pair { -1.0f, "LP" }, std::pair { 0.0f, "Notch" }, std::pair { 1.0f, "HP" }, std::pair { 0.5f, "0.50" } })
	{
		REQUIRE(parameter->getText(parameter->convertTo0to1(value), 16) == text);
		REQUIRE(parameter->convertFrom0to1(parameter->getValueForText(text)) == Catch::Approx(value).margin(1.0e-6));
	}
	setParameter(processor, vekt::mono::parameters::filterMode, 0.25f);
	juce::MemoryBlock state;
	processor.getStateInformation(state);
	setParameter(processor, vekt::mono::parameters::filterMode, -1.0f);
	processor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
	REQUIRE(processor.getParameters().getRawParameterValue(vekt::mono::parameters::filterMode)->load() == Catch::Approx(0.25f).margin(1.0e-3));
}

TEST_CASE("Mono loads schema 8-10 presets and drops the retired Saturated Taps", "[mono][processor][preset][ladder-mode]")
{
	namespace parameters = vekt::mono::parameters;
	juce::ScopedJuceInitialiser_GUI juceInitializer;
	vekt::mono::PluginProcessor processor;
	setParameter(processor, parameters::filterMode, 0.4f);
	setParameter(processor, parameters::lfos[0].filterMode, 30.0f);
	// What each build saved: schema 8 had neither filterSaturatedTaps nor the filter type, 9 added the first, 10 the second;
	// none had K35 (schema 12).
	const auto isIn = [](const auto& identifiers, const char* identifier)
	{ return std::find(identifiers.begin(), identifiers.end(), identifier) != identifiers.end(); };
	std::vector<const char*> withoutType, withoutK35;
	for (const auto* identifier : parameters::soundParameterIds)
	{
		if (isIn(parameters::schema12ParameterIds, identifier)) continue;
		withoutK35.push_back(identifier);
		if (!isIn(parameters::schema10ParameterIds, identifier)) withoutType.push_back(identifier);
	}
	for (const auto schema : { 8, 9, 10 })
	{
		INFO("schema " << schema);
		auto preset = vekt::presets::PresetSchema::create(parameters::presetProductIdentifier, "Old", processor.getParameters(),
			schema == 10 ? std::span<const char* const>(withoutK35) : std::span<const char* const>(withoutType));
		if (schema >= 9) preset.parameters.push_back({ "filterSaturatedTaps", 1.0f });
		preset.soundSchemaVersion = schema;
		setParameter(processor, parameters::filterMode, -1.0f);
		REQUIRE(processor.getPresetSession().prepare(preset).wasOk());
		REQUIRE(preset.soundSchemaVersion == 12);
		REQUIRE(preset.parameters.size() == parameters::soundParameterIds.size());
		REQUIRE(std::none_of(preset.parameters.begin(), preset.parameters.end(), [](const auto& entry)
		{
			return entry.identifier == "filterSaturatedTaps";
		}));
		REQUIRE(vekt::presets::PresetSchema::apply(preset, parameters::presetProductIdentifier,
			processor.getParameters(), parameters::soundParameterIds).wasOk());
		REQUIRE(processor.getParameters().getRawParameterValue(parameters::filterMode)->load() == Catch::Approx(0.4f).margin(1.0e-3));
		REQUIRE(processor.getParameters().getRawParameterValue(parameters::lfos[0].filterMode)->load() == Catch::Approx(30.0f).margin(1.0e-3));
	}
	REQUIRE(processor.getParameters().getParameter("filterSaturatedTaps") == nullptr);
}

TEST_CASE("Mono filter Mode sweeps smoothly and changes the held sound", "[mono][processor][filter][ladder-mode]")
{
	juce::ScopedJuceInitialiser_GUI juceInitializer;
	vekt::mono::PluginProcessor lowPass, swept;
	for (auto* processor : { &lowPass, &swept })
	{
		initializeDryVoice(*processor);
		setParameter(*processor, vekt::mono::parameters::filterCutoff, 800.0f);
		setParameter(*processor, vekt::mono::parameters::filterResonance, 60.0f);
		processor->prepareToPlay(48'000.0, 2400);
	}
	juce::AudioBuffer<float> lowPassBuffer(2, 2400), sweptBuffer(2, 2400);
	juce::MidiBuffer midi;
	midi.addEvent(juce::MidiMessage::noteOn(1, 48, 1.0f), 0);
	renderBlock(lowPass, lowPassBuffer, midi);
	renderBlock(swept, sweptBuffer, midi);
	for (const auto mode : { 0.0f, 1.0f })
	{
		setParameter(swept, vekt::mono::parameters::filterMode, mode);
		renderBlock(lowPass, lowPassBuffer);
		renderBlock(swept, sweptBuffer);
		REQUIRE(swept.coupledWorkSnapshot().nonFinite == 0);
		// The 20 ms ramp starts from the previous response, so the first sample cannot jump.
		REQUIRE(std::abs(sweptBuffer.getSample(0, 0) - lowPassBuffer.getSample(0, 0)) < 0.05f);
		renderBlock(lowPass, lowPassBuffer);
		renderBlock(swept, sweptBuffer);
		juce::AudioBuffer<float> difference(1, 2400);
		for (int sample = 0; sample < 2400; ++sample)
			difference.setSample(0, sample, sweptBuffer.getSample(0, sample) - lowPassBuffer.getSample(0, sample));
		INFO("mode " << mode);
		REQUIRE(rms(difference) > 0.1f * rms(lowPassBuffer));
	}
}

TEST_CASE("Mono contour engine measures analog timing and retrigger continuity", "[mono][processor][contour]")
{
	vekt::mono::ContourEnvelope envelope;
	envelope.setSampleRate(48'000.0);
	envelope.setParameters({ 0.1f, 0.2f, 0.5f, 0.8f });
	envelope.noteOn();
	for (int i = 0; i < 4'800; ++i) envelope.getNextSample();
	REQUIRE(envelope.isActive());
	// At 100 ms the analog attack is at its defined 99% endpoint.
	envelope.reset(); envelope.noteOn();
	for (int i = 0; i < 4'799; ++i) envelope.getNextSample();
	REQUIRE(envelope.getNextSample() == Catch::Approx(1.0f).margin(0.002f));
	for (int i = 0; i < 9'600; ++i) envelope.getNextSample();
	REQUIRE(envelope.getNextSample() == Catch::Approx(0.5f).margin(0.005f));
	envelope.noteOff();
	for (int i = 0; i < 76'802; ++i) envelope.getNextSample();
	REQUIRE_FALSE(envelope.isActive());
	envelope.noteOn();
	for (int i = 0; i < 16'000; ++i) envelope.getNextSample();
	envelope.noteOff();
	for (int i = 0; i < 2'400; ++i) envelope.getNextSample();
	const auto before = envelope.getNextSample();
	envelope.noteOn();
	const auto after = envelope.getNextSample();
	REQUIRE(after >= before);
	REQUIRE(after - before < 0.001f);
}

TEST_CASE("Mono amp and filter contours use their independent release times", "[mono][processor][contour]")
{
	vekt::mono::ContourEnvelope amp, filter;
	for (auto* envelope : { &amp, &filter }) envelope->setSampleRate(1'000.0);
	amp.setParameters({ 0.01f, 0.3f, 1.0f, 0.1f });
	filter.setParameters({ 0.01f, 0.1f, 1.0f, 0.3f });
	amp.noteOn(); filter.noteOn();
	for (int i = 0; i < 20; ++i) { amp.getNextSample(); filter.getNextSample(); }
	amp.noteOff(); filter.noteOff();
	for (int i = 0; i < 210; ++i) { amp.getNextSample(); filter.getNextSample(); }
	REQUIRE_FALSE(amp.isActive());
	REQUIRE(filter.isActive());
	for (int i = 0; i < 410; ++i) filter.getNextSample();
	REQUIRE_FALSE(filter.isActive());
}

TEST_CASE("Mono contour updates held sustain and release without resetting the level", "[mono][processor][contour]")
{
	vekt::mono::ContourEnvelope envelope;
	envelope.setSampleRate(1'000.0);
	envelope.setParameters({ 0.01f, 0.05f, 0.5f, 1.0f });
	envelope.noteOn();
	for (int i = 0; i < 200; ++i) envelope.getNextSample();
	REQUIRE(envelope.getNextSample() == Catch::Approx(0.5f));
	envelope.setParameters({ 0.01f, 0.05f, 0.8f, 1.0f });
	REQUIRE(envelope.getNextSample() == Catch::Approx(0.8f));
	envelope.noteOff();
	for (int i = 0; i < 100; ++i) envelope.getNextSample();
	const auto before = envelope.getNextSample();
	envelope.setParameters({ 0.01f, 0.05f, 0.8f, 0.012f });
	REQUIRE(envelope.getNextSample() < before);
	for (int i = 0; i < 30; ++i) envelope.getNextSample();
	REQUIRE_FALSE(envelope.isActive());
}

TEST_CASE("Mono decay and release tails do not snap at an audible level", "[mono][processor][contour]")
{
	vekt::mono::ContourEnvelope envelope;
	envelope.setSampleRate(1'000.0);
	envelope.setParameters({ 0.0f, 0.2f, 0.0f, 0.2f });
	envelope.noteOn();
	REQUIRE(envelope.getNextSample() == Catch::Approx(1.0f));
	for (int i = 0; i < 200; ++i) (void) envelope.getNextSample();
	const auto decayAtDisplayedTime = envelope.getNextSample();
	REQUIRE(decayAtDisplayedTime > 0.009f);
	REQUIRE(decayAtDisplayedTime < 0.011f);
	float previous = decayAtDisplayedTime;
	for (int i = 0; i < 210; ++i)
	{
		const auto current = envelope.getNextSample();
		REQUIRE(current <= previous);
		REQUIRE(previous - current < 0.0003f);
		previous = current;
	}
	REQUIRE(previous == 0.0f);

	envelope.setParameters({ 0.0f, 0.0f, 1.0f, 0.2f });
	envelope.noteOn();
	for (int i = 0; i < 2; ++i) (void) envelope.getNextSample();
	envelope.noteOff();
	for (int i = 0; i < 200; ++i) (void) envelope.getNextSample();
	const auto releaseAtDisplayedTime = envelope.getNextSample();
	REQUIRE(releaseAtDisplayedTime > 0.009f);
	REQUIRE(releaseAtDisplayedTime < 0.011f);
	previous = releaseAtDisplayedTime;
	for (int i = 0; i < 210; ++i)
	{
		const auto current = envelope.getNextSample();
		REQUIRE(current <= previous);
		REQUIRE(previous - current < 0.0003f);
		previous = current;
	}
	REQUIRE(previous == 0.0f);
	REQUIRE_FALSE(envelope.isActive());
}

TEST_CASE("Mono low-note priority ignores higher keys and returns to the lowest held key", "[mono][processor][midi][contour]")
{
	juce::ScopedJuceInitialiser_GUI juceInitializer;
	vekt::mono::PluginProcessor processor;
	initializeDryVoice(processor);
	setParameter(processor, vekt::mono::parameters::performanceMode, 1.0f);
	setParameter(processor, vekt::mono::parameters::notePriority, 1.0f);
	processor.prepareToPlay(48'000.0, 4800);
	juce::AudioBuffer<float> buffer(2, 4800);
	juce::MidiBuffer midi;
	midi.addEvent(juce::MidiMessage::noteOn(1, 69, 1.0f), 0);
	renderBlock(processor, buffer, midi);
	midi.clear(); midi.addEvent(juce::MidiMessage::noteOn(1, 81, 1.0f), 0);
	renderBlock(processor, buffer, midi);
	auto [frequency, magnitude] = dominantFrequency(buffer, 200.0f, 1'000.0f, 48'000.0f);
	REQUIRE(magnitude > 0.01f);
	REQUIRE(frequency == Catch::Approx(440.0f).margin(5.0f));
	midi.clear(); midi.addEvent(juce::MidiMessage::noteOn(1, 57, 1.0f), 0);
	renderBlock(processor, buffer, midi);
	midi.clear(); midi.addEvent(juce::MidiMessage::noteOff(1, 57), 0);
	renderBlock(processor, buffer, midi);
	std::tie(frequency, magnitude) = dominantFrequency(buffer, 200.0f, 1'000.0f, 48'000.0f);
	REQUIRE(frequency == Catch::Approx(440.0f).margin(5.0f));
}

TEST_CASE("Mono glide follows held keys independently of envelope retrigger", "[mono][processor][midi][glide]")
{
	juce::ScopedJuceInitialiser_GUI juceInitializer;
	for (const auto mode : { 1.0f, 2.0f })
		for (const auto glide : { 0.0f, 1.0f, 2.0f })
			for (const auto gap : { 0, 1, 2 })
			{
				const auto overlap = gap == 0;
				vekt::mono::PluginProcessor processor;
				initializeDryVoice(processor);
				setParameter(processor, vekt::mono::parameters::performanceMode, mode);
				setParameter(processor, vekt::mono::parameters::glideMode, glide);
				setParameter(processor, vekt::mono::parameters::glideTime, 0.5f);
				setParameter(processor, vekt::mono::parameters::ampRelease, gap == 2 ? 0.005f : 2.0f);
				processor.prepareToPlay(48'000.0, 4800);
				juce::AudioBuffer<float> buffer(2, 4800);
				juce::MidiBuffer midi;
				midi.addEvent(juce::MidiMessage::noteOn(1, 57, 1.0f), 0);
				renderBlock(processor, buffer, midi);
				if (!overlap)
				{
					midi.clear();
					midi.addEvent(juce::MidiMessage::noteOff(1, 57), 0);
					renderBlock(processor, buffer, midi);
				}
				midi.clear();
				midi.addEvent(juce::MidiMessage::noteOn(1, 69, 1.0f), 0);
				renderBlock(processor, buffer, midi);
				juce::AudioBuffer<float> settled(1, 2400);
				settled.copyFrom(0, 0, buffer, 0, 2400, 2400);
				const auto [frequency, magnitude] = dominantFrequency(settled, 200.0f, 480.0f, 48'000.0f);
				const auto shouldGlide = glide == 1.0f || (glide == 2.0f && overlap);
				INFO("mode=" << mode << ", glide=" << glide << ", gap=" << gap << ", frequency=" << frequency);
				REQUIRE(magnitude > 0.01f);
				if (shouldGlide) REQUIRE(frequency < 330.0f);
				else REQUIRE(frequency == Catch::Approx(440.0f).margin(5.0f));
			}
}

TEST_CASE("Mono held-key return retains the original velocity", "[mono][processor][midi]")
{
	juce::ScopedJuceInitialiser_GUI juceInitializer;
	for (const auto mode : { 1.0f, 2.0f })
	{
		vekt::mono::PluginProcessor processor;
		initializeDryVoice(processor);
		setParameter(processor, vekt::mono::parameters::performanceMode, mode);
		setParameter(processor, vekt::mono::parameters::ampVelocity, 100.0f);
		setParameter(processor, vekt::mono::parameters::filterVelocity, 80.0f);
		setParameter(processor, vekt::mono::parameters::filterCutoff, 1'000.0f);
		processor.prepareToPlay(48'000.0, 4800);
		juce::AudioBuffer<float> buffer(2, 4800);
		juce::MidiBuffer midi;
		midi.addEvent(juce::MidiMessage::noteOn(1, 57, 0.2f), 0);
		renderBlock(processor, buffer, midi);
		renderBlock(processor, buffer);
		const auto reference = rms(buffer);
		midi.clear();
		midi.addEvent(juce::MidiMessage::noteOn(1, 69, 1.0f), 0);
		renderBlock(processor, buffer, midi);
		midi.clear();
		midi.addEvent(juce::MidiMessage::noteOff(1, 69), 0);
		renderBlock(processor, buffer, midi);
		renderBlock(processor, buffer);
		REQUIRE(reference > 0.001f);
		REQUIRE(rms(buffer) == Catch::Approx(reference).epsilon(0.01f));
	}
}

TEST_CASE("Mono Legato starts a new gate during a release tail", "[mono][processor][midi]")
{
	juce::ScopedJuceInitialiser_GUI juceInitializer;
	vekt::mono::PluginProcessor processor;
	initializeDryVoice(processor);
	setParameter(processor, vekt::mono::parameters::performanceMode, 2.0f);
	setParameter(processor, vekt::mono::parameters::ampRelease, 1.0f);
	processor.prepareToPlay(48'000.0, 4800);
	juce::AudioBuffer<float> buffer(2, 4800);
	juce::MidiBuffer midi;
	midi.addEvent(juce::MidiMessage::noteOn(1, 57, 1.0f), 0);
	renderBlock(processor, buffer, midi);
	midi.clear();
	midi.addEvent(juce::MidiMessage::noteOff(1, 57), 0);
	renderBlock(processor, buffer, midi);
	midi.clear();
	midi.addEvent(juce::MidiMessage::noteOn(1, 69, 1.0f), 0);
	renderBlock(processor, buffer, midi);
	for (int block = 0; block < 15; ++block) renderBlock(processor, buffer);
	REQUIRE(rms(buffer) > 0.05f);
}

TEST_CASE("Mono pitch bend is independent of glide time", "[mono][processor][midi][glide]")
{
	juce::ScopedJuceInitialiser_GUI juceInitializer;
	vekt::mono::PluginProcessor immediate, longGlide;
	for (auto* processor : { &immediate, &longGlide })
	{
		initializeDryVoice(*processor);
		setParameter(*processor, vekt::mono::parameters::glideMode, 1.0f);
		setParameter(*processor, vekt::mono::parameters::pitchBendRange, 12.0f);
		processor->prepareToPlay(48'000.0, 4800);
	}
	setParameter(longGlide, vekt::mono::parameters::glideTime, 5.0f);
	juce::AudioBuffer<float> first(2, 4800), second(2, 4800);
	juce::MidiBuffer midi;
	midi.addEvent(juce::MidiMessage::noteOn(1, 57, 1.0f), 0);
	renderBlock(immediate, first, midi);
	renderBlock(longGlide, second, midi);
	midi.clear();
	midi.addEvent(juce::MidiMessage::pitchWheel(1, 16383), 0);
	renderBlock(immediate, first, midi);
	renderBlock(longGlide, second, midi);
	for (int sample = 0; sample < first.getNumSamples(); ++sample)
		REQUIRE(first.getSample(0, sample) == Catch::Approx(second.getSample(0, sample)).margin(1.0e-6f));
	REQUIRE(sinusoidMagnitude(second, 440.0f, 48'000.0f) > rms(second));
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

TEST_CASE("Mono coupled engine renders deterministically at 1x and 8x", "[mono][processor][ladder-coupled]")
{
	vekt::mono::PluginProcessor first, rerun, oversampled, oversampledRerun;
	REQUIRE(first.getName() == "Vekt Mono");
	for (auto* processor : { &first, &rerun, &oversampled, &oversampledRerun })
	{
		initializeDryVoice(*processor);
		setParameter(*processor, vekt::mono::parameters::filterCutoff, 1'000.0f);
		setParameter(*processor, vekt::mono::parameters::filterResonance, 85.0f);
		setParameter(*processor, vekt::mono::parameters::filterDrive, 12.0f);
	}
	setParameter(first, vekt::mono::parameters::quality, 0.0f);
	setParameter(rerun, vekt::mono::parameters::quality, 0.0f);
	setParameter(oversampled, vekt::mono::parameters::quality, 3.0f);
	setParameter(oversampledRerun, vekt::mono::parameters::quality, 3.0f);
	for (auto* processor : { &first, &rerun, &oversampled, &oversampledRerun })
		processor->prepareToPlay(48'000.0, 128);
	REQUIRE(first.getLatencySamples() == 0);
	REQUIRE(oversampled.getActiveQuality() == 3);
	REQUIRE(oversampled.getLatencySamples() == oversampledRerun.getLatencySamples());
	juce::AudioBuffer<float> a(2, 128), b(2, 128), high(2, 128), highRerun(2, 128);
	juce::MidiBuffer note;
	note.addEvent(juce::MidiMessage::noteOn(1, 48, 0.8f), 0);
	for (int block = 0; block < 8; ++block)
	{
		renderBlock(first, a, note);
		renderBlock(rerun, b, note);
		renderBlock(oversampled, high, note);
		renderBlock(oversampledRerun, highRerun, note);
		if (block == 0) note.clear();
		for (int channel = 0; channel < 2; ++channel)
			for (int sample = 0; sample < 128; ++sample)
			{
				const auto value = a.getSample(channel, sample);
				REQUIRE(std::isfinite(value));
				REQUIRE(value == Catch::Approx(b.getSample(channel, sample)).margin(0.0f));
				REQUIRE(std::bit_cast<std::uint32_t>(high.getSample(channel, sample))
					== std::bit_cast<std::uint32_t>(highRerun.getSample(channel, sample)));
			}
	}
	REQUIRE(first.coupledWorkSnapshot().samples > 0);
	REQUIRE(oversampled.coupledWorkSnapshot().samples > first.coupledWorkSnapshot().samples);
}

TEST_CASE("Mono coupled ladder stays silent and finite across driven notes", "[mono][processor][ladder-coupled]")
{
	for (const auto rate : { 44'100.0, 48'000.0, 96'000.0 })
	{
		vekt::mono::PluginProcessor processor;
		initializeDryVoice(processor);
		setParameter(processor, vekt::mono::parameters::filterResonance, 100.0f);
		setParameter(processor, vekt::mono::parameters::filterDrive, 24.0f);
		processor.prepareToPlay(rate, 127);
		juce::AudioBuffer<float> buffer(2, 127);
		for (int block = 0; block < 4; ++block)
		{
			renderBlock(processor, buffer);
			for (int channel = 0; channel < 2; ++channel)
				for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
					REQUIRE(std::bit_cast<std::uint32_t>(buffer.getSample(channel, sample))
						== std::bit_cast<std::uint32_t>(0.0f));
		}
		juce::MidiBuffer note;
		note.addEvent(juce::MidiMessage::noteOn(1, 72, 0.8f), 0);
		double energy {};
		for (int block = 0; block < 16; ++block)
		{
			renderBlock(processor, buffer, note);
			note.clear();
			for (int channel = 0; channel < 2; ++channel)
				for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
				{
					const auto value = buffer.getSample(channel, sample);
					REQUIRE(std::isfinite(value));
					REQUIRE(std::abs(value) < 24.0f);
					energy += std::abs(value);
				}
		}
		CAPTURE(rate, energy);
		REQUIRE(energy > 0.01);
		processor.releaseResources();
		processor.prepareToPlay(rate, 127);
		REQUIRE(processor.coupledWorkSnapshot().samples == 0);
		renderBlock(processor, buffer);
		for (int channel = 0; channel < 2; ++channel)
			for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
				REQUIRE(std::bit_cast<std::uint32_t>(buffer.getSample(channel, sample))
					== std::bit_cast<std::uint32_t>(0.0f));
	}
}

TEST_CASE("Mono coupled reprepare clears active audio at every retained quality", "[mono][processor][ladder-coupled][quality][reset]")
{
	for (const auto rate : { 44'100.0, 48'000.0, 96'000.0 })
	for (int quality = 0; quality < 4; ++quality)
	{
		CAPTURE(rate, quality);
		vekt::mono::PluginProcessor restarted, fresh;
		for (auto* processor : { &restarted, &fresh })
		{
			initializeDryVoice(*processor);
			setParameter(*processor, vekt::mono::parameters::filterCutoff, 1'000.0f);
			setParameter(*processor, vekt::mono::parameters::filterResonance, 85.0f);
			setParameter(*processor, vekt::mono::parameters::filterDrive, 12.0f);
			setParameter(*processor, vekt::mono::parameters::quality, static_cast<float>(quality));
			processor->prepareToPlay(rate, 128);
		}
		juce::AudioBuffer<float> actual(2, 128), expected(2, 128);
		juce::MidiBuffer note;
		note.addEvent(juce::MidiMessage::noteOn(1, 48, 0.8f), 0);
		for (int block = 0; block < 8; ++block)
		{
			renderBlock(restarted, actual, note);
			note.clear();
		}
		REQUIRE(stereoRms(actual) > 1.0e-4f);
		restarted.releaseResources();
		restarted.prepareToPlay(rate, 128);
		REQUIRE(restarted.getActiveQuality() == quality);
		REQUIRE(restarted.getLatencySamples() == fresh.getLatencySamples());
		for (int block = 0; block < 4; ++block)
		{
			renderBlock(restarted, actual);
			for (int channel = 0; channel < 2; ++channel)
				for (int sample = 0; sample < 128; ++sample)
					REQUIRE(actual.getSample(channel, sample) == 0.0f);
		}
		note.addEvent(juce::MidiMessage::noteOn(1, 55, 0.8f), 32);
		double energy {};
		for (int block = 0; block < 4; ++block)
		{
			renderBlock(restarted, actual, note);
			renderBlock(fresh, expected, note);
			note.clear();
			for (int channel = 0; channel < 2; ++channel)
				for (int sample = 0; sample < 128; ++sample)
				{
					const auto value = actual.getSample(channel, sample);
					REQUIRE(std::isfinite(value));
					REQUIRE(std::abs(value - expected.getSample(channel, sample)) < 1.0e-5f);
					energy += static_cast<double>(value) * value;
				}
		}
		REQUIRE(energy > 0.01);
		const auto work = restarted.coupledWorkSnapshot();
		REQUIRE(work.samples > 0);
		REQUIRE(work.unconverged == 0);
		REQUIRE(work.nonFinite == 0);
	}
}

TEST_CASE("Mono coupled work counters track an ordinary driven processor", "[mono][processor][ladder-coupled]")
{
	vekt::mono::PluginProcessor processor;
	initializeDryVoice(processor);
	setParameter(processor, vekt::mono::parameters::filterCutoff, 1'000.0f);
	setParameter(processor, vekt::mono::parameters::filterResonance, 85.0f);
	setParameter(processor, vekt::mono::parameters::filterDrive, 12.0f);
	processor.prepareToPlay(48'000.0, 128);
	REQUIRE(processor.coupledWorkSnapshot().samples == 0);
	juce::AudioBuffer<float> buffer(2, 128);
	juce::MidiBuffer note;
	note.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
	for (int block = 0; block < 8; ++block)
	{
		renderBlock(processor, buffer, note);
		note.clear();
		for (int channel = 0; channel < 2; ++channel)
			for (int sample = 0; sample < 128; ++sample)
				REQUIRE(std::isfinite(buffer.getSample(channel, sample)));
	}
	REQUIRE(stereoRms(buffer) > 0.001f);
	const auto work = processor.coupledWorkSnapshot();
	REQUIRE(work.samples > 0);
	REQUIRE(work.iterations > 0);
	REQUIRE(work.lineSearchTrials >= work.iterations);
	REQUIRE(work.unconverged == 0);
	REQUIRE(work.nonFinite == 0);
}

TEST_CASE("Mono coupled processor exercises every retained quality", "[mono][processor][ladder-coupled][quality]")
{
	for (int quality = 0; quality <= 3; ++quality)
	{
		vekt::mono::PluginProcessor coupled;
		initializeDryVoice(coupled);
		setParameter(coupled, vekt::mono::parameters::quality, static_cast<float>(quality));
		setParameter(coupled, vekt::mono::parameters::filterCutoff, 1'000.0f);
		setParameter(coupled, vekt::mono::parameters::filterResonance, 85.0f);
		setParameter(coupled, vekt::mono::parameters::filterDrive, 12.0f);
		coupled.prepareToPlay(48'000.0, 128);
		INFO("quality=" << quality);
		REQUIRE(coupled.getActiveQuality() == quality);
		juce::AudioBuffer<float> actual(2, 128);
		juce::MidiBuffer note;
		note.addEvent(juce::MidiMessage::noteOn(1, 48, 0.8f), 0);
		for (int block = 0; block < 4; ++block)
		{
			renderBlock(coupled, actual, note);
			note.clear();
			for (int channel = 0; channel < 2; ++channel)
				for (int sample = 0; sample < actual.getNumSamples(); ++sample)
				{
					const auto value = actual.getSample(channel, sample);
					REQUIRE(std::isfinite(value));
				}
		}
		const auto work = coupled.coupledWorkSnapshot();
		REQUIRE(work.samples > 0);
		REQUIRE(work.unconverged == 0);
		REQUIRE(work.nonFinite == 0);
	}
}

TEST_CASE("Mono coupled reports oversampling latency across retained rates and blocks", "[mono][processor][ladder-coupled][quality][latency]")
{
	const std::array paths {
		vekt::dsp::OversamplingQuality { vekt::dsp::OversamplingFactor::off, vekt::dsp::OversamplingFilter::polyphaseIIR },
		vekt::dsp::OversamplingQuality { vekt::dsp::OversamplingFactor::x2, vekt::dsp::OversamplingFilter::polyphaseIIR },
		vekt::dsp::OversamplingQuality { vekt::dsp::OversamplingFactor::x4, vekt::dsp::OversamplingFilter::polyphaseFIR },
		vekt::dsp::OversamplingQuality { vekt::dsp::OversamplingFactor::x8, vekt::dsp::OversamplingFilter::polyphaseFIR }
	};
	for (const auto rate : { 44'100.0, 48'000.0, 88'200.0, 96'000.0, 192'000.0 })
	for (const auto blockSize : { 128, 257 })
	{
		vekt::dsp::OversamplingBank<float> expected(2);
		expected.prepare(static_cast<std::size_t>(blockSize));
		for (int quality = 0; quality < 4; ++quality)
		{
			CAPTURE(rate, blockSize, quality);
			vekt::mono::PluginProcessor coupled;
			initializeDryVoice(coupled);
			setParameter(coupled, vekt::mono::parameters::quality, static_cast<float>(quality));
			coupled.prepareToPlay(rate, blockSize);
			expected.activate(paths[static_cast<std::size_t>(quality)]);
			const auto latency = expected.getActiveLatencySamples();
			REQUIRE(coupled.getActiveQuality() == quality);
			REQUIRE(coupled.getLatencySamples() == latency);
			juce::AudioBuffer<float> buffer(2, blockSize);
			juce::MidiBuffer note;
			note.addEvent(juce::MidiMessage::noteOn(1, 48, 0.8f), 0);
			for (int block = 0; block < 3; ++block)
			{
				renderBlock(coupled, buffer, note);
				note.clear();
				REQUIRE(coupled.getLatencySamples() == latency);
			}
		}
	}
}

TEST_CASE("Mono coupled quality state recalls into the normal processor", "[mono][processor][ladder-coupled][quality][state]")
{
	for (int quality = 0; quality <= 3; ++quality)
	{
		vekt::mono::PluginProcessor source, restored;
		initializeDryVoice(source);
		setParameter(source, vekt::mono::parameters::quality, static_cast<float>(quality));
		setParameter(source, vekt::mono::parameters::filterCutoff, 1'000.0f);
		setParameter(source, vekt::mono::parameters::filterResonance, 85.0f);
		setParameter(source, vekt::mono::parameters::filterDrive, 12.0f);
		juce::MemoryBlock state;
		source.getStateInformation(state);
		REQUIRE(state.getSize() > 0);
		restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
		for (auto* processor : { &source, &restored })
			processor->prepareToPlay(48'000.0, 128);
		INFO("quality=" << quality);
		REQUIRE(source.getActiveQuality() == quality);
		REQUIRE(restored.getActiveQuality() == quality);
		REQUIRE(source.getLatencySamples() == restored.getLatencySamples());
		juce::AudioBuffer<float> original(2, 128), recalled(2, 128);
		juce::MidiBuffer note;
		note.addEvent(juce::MidiMessage::noteOn(1, 48, 0.8f), 0);
		double energy {};
		for (int block = 0; block < 4; ++block)
		{
			renderBlock(source, original, note);
			renderBlock(restored, recalled, note);
			note.clear();
			for (int channel = 0; channel < 2; ++channel)
				for (int sample = 0; sample < 128; ++sample)
				{
					const auto expected = original.getSample(channel, sample);
					const auto actual = recalled.getSample(channel, sample);
					REQUIRE(std::isfinite(expected));
					REQUIRE(std::isfinite(actual));
					REQUIRE(std::bit_cast<std::uint32_t>(actual) == std::bit_cast<std::uint32_t>(expected));
					energy += static_cast<double>(actual) * actual;
				}
		}
		REQUIRE(energy > 0.01);
		const auto work = restored.coupledWorkSnapshot();
		REQUIRE(work.samples > 0);
		REQUIRE(work.unconverged == 0);
		REQUIRE(work.nonFinite == 0);
	}
}

TEST_CASE("Mono preset loads clear old audio while allowing new notes in the first callback", "[mono][processor][ladder-coupled][preset]")
{
	for (int quality = 0; quality <= 3; ++quality)
	for (int route = 0; route < 5; ++route)
	for (const bool immediateNote : { false, true })
	{
		struct TemporaryPresetDirectory
		{
			juce::File path = juce::File::getSpecialLocation(juce::File::tempDirectory)
				.getChildFile("vekt-mono-preset-isolation-" + juce::Uuid().toString()); // unique across parallel test processes
			~TemporaryPresetDirectory() { path.deleteRecursively(); }
		} directory;
		vekt::presets::FilePresetRepository repository(directory.path);
		vekt::mono::PluginProcessor changed, fresh;
		if (route == 4)
		{
			vekt::presets::Preset preset;
			REQUIRE(changed.getPresetSession().library().loadFactoryPreset(24, preset).wasOk());
			preset.identifier = "mono-isolation-user-test";
			preset.name = "Isolation Test Bass";
			REQUIRE(repository.save(preset).wasOk());
			for (auto* processor : { &changed, &fresh })
				processor->getPresetSession().library().setUserRepository(&repository);
		}
		for (auto* processor : { &changed, &fresh })
		{
			setParameter(*processor, vekt::mono::parameters::quality, static_cast<float>(quality));
			processor->prepareToPlay(48'000.0, 128);
		}
		CAPTURE(quality, route, immediateNote);
		juce::AudioBuffer<float> oldNote(2, 128), actual(2, 128), expected(2, 128);
		juce::MidiBuffer note;
		note.addEvent(juce::MidiMessage::noteOn(1, 48, 0.8f), 0);
		for (int block = 0; block < 8; ++block)
		{
			renderBlock(changed, oldNote, note);
			note.clear();
		}
		REQUIRE(stereoRms(oldNote) > 1.0e-4f);
		const auto load = [route](vekt::mono::PluginProcessor& processor)
		{
			if (route == 0) processor.setCurrentProgram(24); // Classic Three Bass
			else if (route == 1)
			{
				vekt::presets::Preset preset;
				auto& session = processor.getPresetSession();
				REQUIRE(session.library().loadFactoryPreset(24, preset).wasOk());
				REQUIRE(session.load(preset.identifier, vekt::presets::PresetOrigin::factory).wasOk());
			}
			else if (route == 2)
			{
				processor.setCurrentProgram(23);
				REQUIRE(processor.loadNextPreset().wasOk());
			}
			else if (route == 3)
			{
				processor.setCurrentProgram(24);
				REQUIRE(processor.loadPreviousPreset().wasOk());
			}
			else REQUIRE(processor.getPresetSession().load("mono-isolation-user-test", vekt::presets::PresetOrigin::user).wasOk());
		};
		load(changed);
		load(fresh);
		const auto program = route == 3 ? 23 : 24;
		if (route != 4)
		{
			REQUIRE(changed.getCurrentProgram() == program);
			REQUIRE(fresh.getCurrentProgram() == program);
		}
		else REQUIRE(changed.getPresetSession().origin() == vekt::presets::PresetOrigin::user);
		REQUIRE(changed.getActiveQuality() == quality);
		REQUIRE(changed.getLatencySamples() == fresh.getLatencySamples());
		REQUIRE(changed.getParameters().getRawParameterValue(vekt::mono::parameters::filterCutoff)->load()
			== Catch::Approx(fresh.getParameters().getRawParameterValue(vekt::mono::parameters::filterCutoff)->load()));
		if (immediateNote) note.addEvent(juce::MidiMessage::noteOn(1, 55, 0.8f), 32);
		renderBlock(changed, actual, note);
		renderBlock(fresh, expected, note);
		for (int channel = 0; channel < 2; ++channel)
			for (int sample = 0; sample < (immediateNote ? 32 : 128); ++sample)
			{
				REQUIRE(actual.getSample(channel, sample) == 0.0f);
			}
		if (!immediateNote) continue;
		double firstBlockEnergy {};
		for (int channel = 0; channel < 2; ++channel)
			for (int sample = 32; sample < 128; ++sample)
			{
				const auto value = actual.getSample(channel, sample);
				REQUIRE(std::isfinite(value));
				REQUIRE(std::abs(value - expected.getSample(channel, sample)) < 1.0e-5f);
				firstBlockEnergy += static_cast<double>(value) * value;
			}
		REQUIRE(firstBlockEnergy > 0.0);
		note.clear();
		double energy {};
		for (int block = 0; block < 4; ++block)
		{
			renderBlock(changed, actual, note);
			renderBlock(fresh, expected, note);
			note.clear();
			for (int channel = 0; channel < 2; ++channel)
				for (int sample = 0; sample < 128; ++sample)
				{
					const auto value = actual.getSample(channel, sample);
					REQUIRE(std::isfinite(value));
					REQUIRE(std::isfinite(expected.getSample(channel, sample)));
					REQUIRE(std::abs(value - expected.getSample(channel, sample)) < 1.0e-5f);
					energy += static_cast<double>(value) * value;
				}
		}
		REQUIRE(energy > 0.001);
		const auto work = changed.coupledWorkSnapshot();
		REQUIRE(work.samples > 0);
		REQUIRE(work.unconverged == 0);
		REQUIRE(work.nonFinite == 0);
	}
}

TEST_CASE("Mono coupled callbacks remain finite across MIDI and control boundaries", "[mono][processor][ladder-coupled]")
{
	for (const auto rate : { 44'100.0, 48'000.0 })
		for (const auto blockSize : { 128, 257 })
		{
			vekt::mono::PluginProcessor coupled;
			initializeDryVoice(coupled);
			setParameter(coupled, vekt::mono::parameters::performanceMode, 0.0f);
			setParameter(coupled, vekt::mono::parameters::filterCutoff, 1'000.0f);
			setParameter(coupled, vekt::mono::parameters::filterResonance, 85.0f);
			setParameter(coupled, vekt::mono::parameters::filterDrive, 12.0f);
			coupled.prepareToPlay(rate, blockSize);
			juce::AudioBuffer<float> actual(2, blockSize);
			double energy {};
			for (int block = 0; block < 24; ++block)
			{
				if (block == 4 || block == 12 || block == 18)
					{
						setParameter(coupled, vekt::mono::parameters::filterCutoff,
							block == 4 ? 20'000.0f : block == 12 ? 20.0f : 1'000.0f);
						setParameter(coupled, vekt::mono::parameters::filterResonance,
							block == 12 ? 100.0f : 85.0f);
						setParameter(coupled, vekt::mono::parameters::filterDrive,
							block == 4 ? 24.0f : block == 12 ? 0.0f : 12.0f);
						}
				juce::MidiBuffer midi;
				if (block == 0) midi.addEvent(juce::MidiMessage::noteOn(1, 48, 0.8f), 0);
				if (block == 2) midi.addEvent(juce::MidiMessage::noteOn(1, 55, 0.7f), blockSize / 2);
				if (block == 6) midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), blockSize - 1);
				if (block == 10) midi.addEvent(juce::MidiMessage::noteOff(1, 55), blockSize / 3);
				if (block == 15) midi.addEvent(juce::MidiMessage::noteOff(1, 48), 1);
				if (block == 20) midi.addEvent(juce::MidiMessage::noteOff(1, 60), blockSize / 2);
				renderBlock(coupled, actual, midi);
				for (int channel = 0; channel < 2; ++channel)
					for (int sample = 0; sample < blockSize; ++sample)
					{
						const auto value = actual.getSample(channel, sample);
						CAPTURE(rate, blockSize, block, channel, sample, value);
						REQUIRE(std::isfinite(value));
						energy += static_cast<double>(value) * value;
					}
			}
			CAPTURE(rate, blockSize, energy);
			REQUIRE(energy > 0.01);
			const auto work = coupled.coupledWorkSnapshot();
			REQUIRE(work.samples > 0);
			REQUIRE(work.unconverged == 0);
			REQUIRE(work.nonFinite == 0);
		}
}

TEST_CASE("Mono coupled quality changes cut sustained notes immediately", "[mono][processor][ladder-coupled][quality]")
{
	for (int initial = 0; initial < 4; ++initial)
	for (int target = 0; target < 4; ++target)
	{
		if (initial == target) continue;
		CAPTURE(initial, target);
		vekt::mono::PluginProcessor coupled, fresh;
		for (auto* processor : { &coupled, &fresh })
		{
			initializeDryVoice(*processor);
			setParameter(*processor, vekt::mono::parameters::ampRelease, 0.005f);
			setParameter(*processor, vekt::mono::parameters::filterCutoff, 1'000.0f);
			setParameter(*processor, vekt::mono::parameters::filterResonance, 85.0f);
			setParameter(*processor, vekt::mono::parameters::filterDrive, 12.0f);
			setParameter(*processor, vekt::mono::parameters::quality,
				static_cast<float>(processor == &fresh ? target : initial));
			processor->prepareToPlay(48'000.0, 128);
		}
		juce::AudioBuffer<float> actual(2, 128);
		juce::MidiBuffer held;
		held.addEvent(juce::MidiMessage::controllerEvent(1, 64, 127), 0);
		held.addEvent(juce::MidiMessage::noteOn(1, 48, 0.8f), 0);
		held.addEvent(juce::MidiMessage::noteOff(1, 48), 64);
		renderBlock(coupled, actual, held);
		setParameter(coupled, vekt::mono::parameters::quality, static_cast<float>(target));
		renderBlock(coupled, actual);
		REQUIRE(coupled.getActiveQuality() == target);
		REQUIRE(coupled.getLatencySamples() == fresh.getLatencySamples());
		for (int channel = 0; channel < 2; ++channel)
			for (int sample = 0; sample < 128; ++sample)
				REQUIRE(actual.getSample(channel, sample) == 0.0f);
		juce::MidiBuffer nextNote;
		nextNote.addEvent(juce::MidiMessage::noteOn(1, 55, 0.8f), 0);
		double energy {};
		for (int block = 0; block < 4; ++block)
		{
			renderBlock(coupled, actual, nextNote);
			nextNote.clear();
			for (int channel = 0; channel < 2; ++channel)
				for (int sample = 0; sample < 128; ++sample)
				{
					const auto value = actual.getSample(channel, sample);
					REQUIRE(std::isfinite(value));
					energy += static_cast<double>(value) * value;
				}
		}
		REQUIRE(energy > 0.01);
		const auto work = coupled.coupledWorkSnapshot();
		REQUIRE(work.samples > 0);
		REQUIRE(work.unconverged == 0);
		REQUIRE(work.nonFinite == 0);
	}
}

TEST_CASE("Mono coupled idle quality changes cover every ordered pair", "[mono][processor][ladder-coupled][quality]")
{
	for (int initial = 0; initial < 4; ++initial)
	for (int target = 0; target < 4; ++target)
	{
		if (initial == target) continue;
		CAPTURE(initial, target);
		vekt::mono::PluginProcessor changed, fresh;
		for (auto* processor : { &changed, &fresh })
		{
			initializeDryVoice(*processor);
			setParameter(*processor, vekt::mono::parameters::filterCutoff, 1'000.0f);
			setParameter(*processor, vekt::mono::parameters::filterResonance, 85.0f);
			setParameter(*processor, vekt::mono::parameters::filterDrive, 12.0f);
			setParameter(*processor, vekt::mono::parameters::quality,
				static_cast<float>(processor == &fresh ? target : initial));
			processor->prepareToPlay(48'000.0, 128);
		}
		juce::AudioBuffer<float> actual(2, 128), expected(2, 128);
		setParameter(changed, vekt::mono::parameters::quality, static_cast<float>(target));
		renderBlock(changed, actual);
		REQUIRE(changed.getActiveQuality() == target);
		REQUIRE(changed.getLatencySamples() == fresh.getLatencySamples());
		for (int channel = 0; channel < 2; ++channel)
			for (int sample = 0; sample < 128; ++sample)
				REQUIRE(actual.getSample(channel, sample) == 0.0f);
		juce::MidiBuffer note;
		note.addEvent(juce::MidiMessage::noteOn(1, 55, 0.8f), 32);
		double energy {};
		for (int block = 0; block < 4; ++block)
		{
			renderBlock(changed, actual, note);
			renderBlock(fresh, expected, note);
			note.clear();
			for (int channel = 0; channel < 2; ++channel)
				for (int sample = 0; sample < 128; ++sample)
				{
					const auto value = actual.getSample(channel, sample);
					REQUIRE(std::isfinite(value));
					REQUIRE(std::abs(value - expected.getSample(channel, sample)) < 1.0e-5f);
					energy += static_cast<double>(value) * value;
				}
		}
		REQUIRE(energy > 0.01);
		const auto work = changed.coupledWorkSnapshot();
		REQUIRE(work.samples > 0);
		REQUIRE(work.unconverged == 0);
		REQUIRE(work.nonFinite == 0);
	}
}

TEST_CASE("Mono publishes post-output-gain stereo peaks", "[mono][processor][meter]")
{
	vekt::mono::PluginProcessor processor;
	setParameter(processor, vekt::mono::parameters::masterOutput, -6.0f);
	processor.prepareToPlay(48'000.0, 512);
	juce::AudioBuffer<float> buffer(2, 512);
	juce::MidiBuffer midi;
	midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
	processor.processBlock(buffer, midi);
	const auto peaks = processor.consumeOutputPeaks();
	REQUIRE(std::max(peaks[0], peaks[1]) > 0.0f);
	REQUIRE(peaks[0] == Catch::Approx(buffer.getMagnitude(0, 0, buffer.getNumSamples())).margin(1.0e-6f));
	REQUIRE(peaks[1] == Catch::Approx(buffer.getMagnitude(1, 0, buffer.getNumSamples())).margin(1.0e-6f));
	const auto consumed = processor.consumeOutputPeaks();
	REQUIRE(consumed[0] == 0.0f);
	REQUIRE(consumed[1] == 0.0f);
}

TEST_CASE("Mono Ladder cutoff responds smoothly while a note is held", "[mono][processor][filter]")
{
	vekt::mono::PluginProcessor processor;
	setParameter(processor, vekt::mono::parameters::osc1Morph, 2.0f);
	setParameter(processor, vekt::mono::parameters::osc2Level, 0.0f);
	setParameter(processor, vekt::mono::parameters::osc3Level, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterEnvelopeAmount, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterVelocity, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterKeyTracking, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterResonance, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterCutoff, 120.0f);
	processor.prepareToPlay(48'000.0, 4096);
	juce::AudioBuffer<float> buffer(2, 4096);
	juce::MidiBuffer noteOn;
	noteOn.addEvent(juce::MidiMessage::noteOn(1, 48, 1.0f), 0);
	renderBlock(processor, buffer, noteOn);
	renderBlock(processor, buffer);
	const auto closedBrightness = differenceRms(buffer);

	setParameter(processor, vekt::mono::parameters::filterCutoff, 12'000.0f);
	renderBlock(processor, buffer);
	for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
		REQUIRE(std::isfinite(buffer.getSample(0, sample)));
	const auto openBrightness = differenceRms(buffer);
	REQUIRE(openBrightness > closedBrightness * 3.0f);
}

TEST_CASE("Mono Ladder emphasis builds a resonant peak and remains stable", "[mono][processor][filter]")
{
	auto render = [](float emphasis)
	{
		vekt::mono::PluginProcessor processor;
		setParameter(processor, vekt::mono::parameters::osc1Level, 0.0f);
		setParameter(processor, vekt::mono::parameters::osc2Level, 0.0f);
		setParameter(processor, vekt::mono::parameters::osc3Level, 0.0f);
		setParameter(processor, vekt::mono::parameters::noiseType, 1.0f);
		setParameter(processor, vekt::mono::parameters::noiseLevel, 50.0f);
		setParameter(processor, vekt::mono::parameters::filterCutoff, 1'000.0f);
		setParameter(processor, vekt::mono::parameters::filterEnvelopeAmount, 0.0f);
		setParameter(processor, vekt::mono::parameters::filterVelocity, 0.0f);
		setParameter(processor, vekt::mono::parameters::filterKeyTracking, 0.0f);
		setParameter(processor, vekt::mono::parameters::filterResonance, emphasis);
		processor.prepareToPlay(48'000.0, 4096);
		juce::AudioBuffer<float> buffer(2, 4096);
		juce::MidiBuffer noteOn;
		noteOn.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
		renderBlock(processor, buffer, noteOn);
		renderBlock(processor, buffer);
		return std::pair { sinusoidMagnitude(buffer, 1'000.0f, 48'000.0f), rms(buffer) };
	};
	const auto [flatPeak, flatRms] = render(0.0f);
	const auto [emphasizedPeak, emphasizedRms] = render(100.0f);
	REQUIRE(std::isfinite(emphasizedRms));
	REQUIRE(emphasizedRms < 2.0f);
	// The raw coupled tap can lose overall level while the cutoff peak grows.
	REQUIRE(flatRms > 0.001f);
	REQUIRE(emphasizedPeak > flatPeak * 1.5f);
}

TEST_CASE("Mono uncompensated Ladder loses passband level with emphasis", "[mono][processor][filter][qcomp][slow]")
{
	juce::ScopedJuceInitialiser_GUI juceInitializer;
	auto levelFor = [](float oscillatorLevel, float cutoff, float emphasis)
	{
		vekt::mono::PluginProcessor processor;
		initializeDryVoice(processor);
		setParameter(processor, vekt::mono::parameters::osc1Level, oscillatorLevel);
		setParameter(processor, vekt::mono::parameters::osc1Morph, 0.0f);
		setParameter(processor, vekt::mono::parameters::osc2Level, 0.0f);
		setParameter(processor, vekt::mono::parameters::osc3Level, 0.0f);
		setParameter(processor, vekt::mono::parameters::filterCutoff, cutoff);
		setParameter(processor, vekt::mono::parameters::filterEnvelopeAmount, 0.0f);
		setParameter(processor, vekt::mono::parameters::filterVelocity, 0.0f);
		setParameter(processor, vekt::mono::parameters::filterKeyTracking, 0.0f);
		setParameter(processor, vekt::mono::parameters::filterDrive, 0.0f);
		setParameter(processor, vekt::mono::parameters::filterResonance, emphasis);
		processor.prepareToPlay(48'000.0, 4096);
		juce::AudioBuffer<float> buffer(2, 4096);
		juce::MidiBuffer noteOn;
		noteOn.addEvent(juce::MidiMessage::noteOn(1, 36, 1.0f), 0);
		renderBlock(processor, buffer, noteOn);
		for (int block = 0; block < 12; ++block) renderBlock(processor, buffer);
		return sinusoidMagnitude(buffer, 65.4064f, 48'000.0f);
	};
	for (const auto oscillatorLevel : { 20.0f, 50.0f, 100.0f })
		for (const auto cutoff : { 500.0f, 1'000.0f, 4'000.0f })
		{
			const auto reference = levelFor(oscillatorLevel, cutoff, 0.0f);
			REQUIRE(reference > 0.01f);
			for (const auto emphasis : { 25.0f, 50.0f, 65.0f })
			{
				const auto level = levelFor(oscillatorLevel, cutoff, emphasis);
				INFO("oscillator=" << oscillatorLevel << "%, cutoff=" << cutoff << " Hz, emphasis=" << emphasis
					<< "%, fundamental=" << level << ", ratio=" << level / reference);
				CHECK(level > reference * 0.05f);
				CHECK(level < reference * 0.7f);
			}
		}
}

TEST_CASE("Mono Ladder self-oscillates at maximum emphasis", "[mono][processor][filter][slow]")
{
	for (const auto quality : { 0.0f, 1.0f })
		for (const auto sampleRate : { 44'100.0f, 48'000.0f, 96'000.0f })
			for (const auto cutoff : { 250.0f, 1'000.0f, 4'000.0f })
		{
			vekt::mono::PluginProcessor processor;
			setParameter(processor, vekt::mono::parameters::quality, quality);
			setParameter(processor, vekt::mono::parameters::osc1Level, 0.0f);
			setParameter(processor, vekt::mono::parameters::osc2Level, 0.0f);
			setParameter(processor, vekt::mono::parameters::osc3Level, 0.0f);
			setParameter(processor, vekt::mono::parameters::noiseType, 1.0f);
			setParameter(processor, vekt::mono::parameters::noiseLevel, 5.0f);
			setParameter(processor, vekt::mono::parameters::filterCutoff, cutoff);
			setParameter(processor, vekt::mono::parameters::filterEnvelopeAmount, 0.0f);
			setParameter(processor, vekt::mono::parameters::filterVelocity, 0.0f);
			setParameter(processor, vekt::mono::parameters::filterKeyTracking, 0.0f);
			setParameter(processor, vekt::mono::parameters::filterDrive, 0.0f);
			setParameter(processor, vekt::mono::parameters::filterResonance, 100.0f);
			setParameter(processor, vekt::mono::parameters::ampSustain, 100.0f);
			setParameter(processor, vekt::mono::parameters::ampVelocity, 0.0f);
			setParameter(processor, vekt::mono::parameters::unison, 0.0f);
			setParameter(processor, vekt::mono::parameters::voiceWidth, 0.0f);
			setParameter(processor, vekt::mono::parameters::masterOutput, 0.0f);
			processor.prepareToPlay(sampleRate, 4096);
			juce::AudioBuffer<float> buffer(2, 4096);
			juce::MidiBuffer noteOn;
			noteOn.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
			renderBlock(processor, buffer, noteOn);
			// A delay-free nonlinear ladder does not spontaneously leave an exact
			// zero state. Excite it once, then observe the unforced ringdown.
			setParameter(processor, vekt::mono::parameters::noiseLevel, 0.0f);
			for (int block = 0; block < 24; ++block) renderBlock(processor, buffer);
			const auto settledRms = rms(buffer);
			const auto selfOscPowerRms = stereoPowerRms(buffer);
			const auto [frequency, magnitude] = dominantFrequency(buffer, cutoff * 0.9f, cutoff * 1.1f, sampleRate);
			const auto secondHarmonic = sinusoidMagnitude(buffer, frequency * 2.0f, sampleRate);
			const auto thirdHarmonic = sinusoidMagnitude(buffer, frequency * 3.0f, sampleRate);
			INFO("quality=" << quality << ", sample rate=" << sampleRate << ", cutoff=" << cutoff << ", fundamental=" << frequency
				<< " Hz / " << magnitude << ", second=" << secondHarmonic << ", third=" << thirdHarmonic
				<< ", left RMS=" << settledRms << ", stereo power RMS=" << selfOscPowerRms);
			REQUIRE(selfOscPowerRms > 0.1f);
			REQUIRE(settledRms < 1.0f);
			REQUIRE(frequency == Catch::Approx(cutoff).margin(cutoff * 0.03f));
			REQUIRE(magnitude > settledRms);
			REQUIRE(secondHarmonic < magnitude * 0.1f);
			REQUIRE(thirdHarmonic < magnitude * 0.2f);
			renderBlock(processor, buffer);
			REQUIRE(rms(buffer) >= settledRms * 0.9f);
		}
}

TEST_CASE("Mono Ladder self-oscillation is audible through a preset-style voice path", "[mono][processor][filter]")
{
	vekt::mono::PluginProcessor processor;
	setParameter(processor, vekt::mono::parameters::osc1Level, 0.0f);
	setParameter(processor, vekt::mono::parameters::osc2Level, 0.0f);
	setParameter(processor, vekt::mono::parameters::osc3Level, 0.0f);
	setParameter(processor, vekt::mono::parameters::noiseType, 1.0f);
	setParameter(processor, vekt::mono::parameters::noiseLevel, 5.0f);
	setParameter(processor, vekt::mono::parameters::filterCutoff, 1'000.0f);
	setParameter(processor, vekt::mono::parameters::filterEnvelopeAmount, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterVelocity, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterKeyTracking, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterDrive, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterResonance, 100.0f);
	setParameter(processor, vekt::mono::parameters::ampSustain, 64.0f);
	setParameter(processor, vekt::mono::parameters::ampVelocity, 55.0f);
	setParameter(processor, vekt::mono::parameters::unison, 1.0f);
	setParameter(processor, vekt::mono::parameters::unisonSpread, 48.0f);
	setParameter(processor, vekt::mono::parameters::voiceWidth, 18.0f);
	setParameter(processor, vekt::mono::parameters::masterOutput, -7.0f);
	processor.prepareToPlay(48'000.0, 4096);
	juce::AudioBuffer<float> buffer(2, 4096);
	juce::MidiBuffer noteOn;
	noteOn.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
	renderBlock(processor, buffer, noteOn);
	setParameter(processor, vekt::mono::parameters::noiseLevel, 0.0f);
	for (int block = 0; block < 12; ++block) renderBlock(processor, buffer);
	REQUIRE(stereoPowerRms(buffer) > 0.025f);
}

TEST_CASE("Mono Ladder enters self-oscillation when emphasis reaches maximum in real time", "[mono][processor][filter]")
{
	vekt::mono::PluginProcessor processor;
	setParameter(processor, vekt::mono::parameters::osc1Level, 0.0f);
	setParameter(processor, vekt::mono::parameters::osc2Level, 0.0f);
	setParameter(processor, vekt::mono::parameters::osc3Level, 0.0f);
	setParameter(processor, vekt::mono::parameters::noiseType, 0.0f);
	setParameter(processor, vekt::mono::parameters::noiseLevel, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterCutoff, 1'000.0f);
	setParameter(processor, vekt::mono::parameters::filterEnvelopeAmount, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterVelocity, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterKeyTracking, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterDrive, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterResonance, 0.0f);
	setParameter(processor, vekt::mono::parameters::ampSustain, 100.0f);
	setParameter(processor, vekt::mono::parameters::ampVelocity, 0.0f);
	setParameter(processor, vekt::mono::parameters::unison, 0.0f);
	setParameter(processor, vekt::mono::parameters::voiceWidth, 0.0f);
	setParameter(processor, vekt::mono::parameters::masterOutput, 0.0f);
	processor.prepareToPlay(48'000.0, 512);
	juce::AudioBuffer<float> buffer(2, 512);
	juce::MidiBuffer noteOn;
	noteOn.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
	renderBlock(processor, buffer, noteOn);
	for (int block = 0; block < 8; ++block) renderBlock(processor, buffer);
	REQUIRE(stereoRms(buffer) < 1.0e-6f);

	setParameter(processor, vekt::mono::parameters::filterResonance, 100.0f);
	for (int block = 0; block < 120; ++block) renderBlock(processor, buffer);
	// The coupled model preserves the zero equilibrium without excitation.
	REQUIRE(stereoRms(buffer) == 0.0f);
	setParameter(processor, vekt::mono::parameters::noiseType, 1.0f);
	setParameter(processor, vekt::mono::parameters::noiseLevel, 5.0f);
	renderBlock(processor, buffer);
	setParameter(processor, vekt::mono::parameters::noiseLevel, 0.0f);
	for (int block = 0; block < 120; ++block) renderBlock(processor, buffer);
	REQUIRE(stereoPowerRms(buffer) > 0.1f);
	const auto [frequency, magnitude] = dominantFrequency(buffer, 700.0f, 1'400.0f, 48'000.0f);
	REQUIRE(frequency > 750.0f);
	REQUIRE(frequency < 1'300.0f);
	REQUIRE(magnitude > rms(buffer));
}

TEST_CASE("Mono Ladder develops an audible cutoff tone when GUI-style emphasis is raised over an active oscillator", "[mono][processor][filter]")
{
	vekt::mono::PluginProcessor processor;
	setParameter(processor, vekt::mono::parameters::osc1Level, 45.0f);
	setParameter(processor, vekt::mono::parameters::osc1Morph, 0.0f);
	setParameter(processor, vekt::mono::parameters::osc2Level, 0.0f);
	setParameter(processor, vekt::mono::parameters::osc3Level, 0.0f);
	setParameter(processor, vekt::mono::parameters::noiseType, 0.0f);
	setParameter(processor, vekt::mono::parameters::noiseLevel, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterCutoff, 1'000.0f);
	setParameter(processor, vekt::mono::parameters::filterEnvelopeAmount, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterVelocity, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterKeyTracking, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterDrive, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterResonance, 0.0f);
	setParameter(processor, vekt::mono::parameters::ampSustain, 100.0f);
	setParameter(processor, vekt::mono::parameters::ampVelocity, 0.0f);
	setParameter(processor, vekt::mono::parameters::unison, 0.0f);
	setParameter(processor, vekt::mono::parameters::voiceWidth, 0.0f);
	setParameter(processor, vekt::mono::parameters::masterOutput, 0.0f);
	processor.prepareToPlay(48'000.0, 512);
	juce::AudioBuffer<float> buffer(2, 512);
	juce::MidiBuffer noteOn;
	noteOn.addEvent(juce::MidiMessage::noteOn(1, 57, 1.0f), 0);
	renderBlock(processor, buffer, noteOn);
	for (int block = 0; block < 8; ++block) renderBlock(processor, buffer);
	const auto lowResonanceCutoffTone = sinusoidMagnitude(buffer, 1'000.0f, 48'000.0f);

	setParameter(processor, vekt::mono::parameters::filterResonance, 100.0f);
	for (int block = 0; block < 120; ++block) renderBlock(processor, buffer);
	const auto highResonanceCutoffTone = sinusoidMagnitude(buffer, 1'000.0f, 48'000.0f);
	const auto [frequency, magnitude] = dominantFrequency(buffer, 700.0f, 1'400.0f, 48'000.0f);
	INFO("low cutoff tone=" << lowResonanceCutoffTone << ", high cutoff tone=" << highResonanceCutoffTone
		<< ", dominant=" << frequency << " Hz / " << magnitude << ", rms=" << rms(buffer));
	REQUIRE(highResonanceCutoffTone > 0.001f);
	REQUIRE(highResonanceCutoffTone > lowResonanceCutoffTone * 10.0f);
	REQUIRE(frequency > 750.0f);
	REQUIRE(frequency < 1'300.0f);
	REQUIRE(magnitude > rms(buffer));
}

TEST_CASE("Mono Ladder does not self-oscillate below the upper emphasis range", "[mono][processor][filter]")
{
	vekt::mono::PluginProcessor processor;
	setParameter(processor, vekt::mono::parameters::osc1Level, 0.0f);
	setParameter(processor, vekt::mono::parameters::osc2Level, 0.0f);
	setParameter(processor, vekt::mono::parameters::osc3Level, 0.0f);
	setParameter(processor, vekt::mono::parameters::noiseType, 0.0f);
	setParameter(processor, vekt::mono::parameters::noiseLevel, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterCutoff, 1'000.0f);
	setParameter(processor, vekt::mono::parameters::filterEnvelopeAmount, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterVelocity, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterKeyTracking, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterResonance, 70.0f);
	setParameter(processor, vekt::mono::parameters::ampSustain, 100.0f);
	setParameter(processor, vekt::mono::parameters::ampVelocity, 0.0f);
	setParameter(processor, vekt::mono::parameters::unison, 0.0f);
	setParameter(processor, vekt::mono::parameters::voiceWidth, 0.0f);
	setParameter(processor, vekt::mono::parameters::masterOutput, 0.0f);
	processor.prepareToPlay(48'000.0, 4096);
	juce::AudioBuffer<float> buffer(2, 4096);
	juce::MidiBuffer noteOn;
	noteOn.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
	renderBlock(processor, buffer, noteOn);
	for (int block = 0; block < 12; ++block) renderBlock(processor, buffer);
	REQUIRE(stereoRms(buffer) < 1.0e-4f);
}

TEST_CASE("Mono Ladder keyboard tracking follows one octave per keyboard octave", "[mono][processor][filter]")
{
	auto brightnessFor = [](int note)
	{
		vekt::mono::PluginProcessor processor;
		setParameter(processor, vekt::mono::parameters::osc1Level, 0.0f);
		setParameter(processor, vekt::mono::parameters::noiseType, 1.0f);
		setParameter(processor, vekt::mono::parameters::noiseLevel, 50.0f);
		setParameter(processor, vekt::mono::parameters::filterCutoff, 500.0f);
		setParameter(processor, vekt::mono::parameters::filterEnvelopeAmount, 0.0f);
		setParameter(processor, vekt::mono::parameters::filterVelocity, 0.0f);
		setParameter(processor, vekt::mono::parameters::filterKeyTracking, 100.0f);
		setParameter(processor, vekt::mono::parameters::filterResonance, 0.0f);
		processor.prepareToPlay(48'000.0, 4096);
		juce::AudioBuffer<float> buffer(2, 4096);
		juce::MidiBuffer noteOn;
		noteOn.addEvent(juce::MidiMessage::noteOn(1, note, 1.0f), 0);
		renderBlock(processor, buffer, noteOn);
		renderBlock(processor, buffer);
		return differenceRms(buffer);
	};
	REQUIRE(brightnessFor(72) > brightnessFor(48) * 1.8f);
}

TEST_CASE("Mono Ladder contour is unipolar with a wide full-scale sweep", "[mono][processor][filter]")
{
	auto brightnessFor = [](float contour)
	{
		vekt::mono::PluginProcessor processor;
		setParameter(processor, vekt::mono::parameters::osc1Level, 0.0f);
		setParameter(processor, vekt::mono::parameters::noiseType, 1.0f);
		setParameter(processor, vekt::mono::parameters::noiseLevel, 50.0f);
		setParameter(processor, vekt::mono::parameters::filterCutoff, 1'000.0f);
		setParameter(processor, vekt::mono::parameters::filterEnvelopeAmount, contour);
		setParameter(processor, vekt::mono::parameters::filterAttack, 0.0005f);
		setParameter(processor, vekt::mono::parameters::filterSustain, 100.0f);
		setParameter(processor, vekt::mono::parameters::filterVelocity, 0.0f);
		setParameter(processor, vekt::mono::parameters::filterKeyTracking, 0.0f);
		processor.prepareToPlay(48'000.0, 4096);
		juce::AudioBuffer<float> buffer(2, 4096);
		juce::MidiBuffer noteOn;
		noteOn.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
		renderBlock(processor, buffer, noteOn);
		renderBlock(processor, buffer);
		return differenceRms(buffer);
	};
	vekt::mono::PluginProcessor processor;
	auto* parameter = processor.getParameters().getParameter(vekt::mono::parameters::filterEnvelopeAmount);
	REQUIRE(parameter != nullptr);
	REQUIRE(parameter->convertFrom0to1(0.0f) == 0.0f);
	REQUIRE(parameter->convertFrom0to1(1.0f) == 100.0f);
	REQUIRE(brightnessFor(100.0f) > brightnessFor(0.0f) * 5.0f);
}

TEST_CASE("Mono Ladder drive adds harmonics without acting as output gain", "[mono][processor][filter]")
{
	auto render = [](float drive)
	{
		vekt::mono::PluginProcessor processor;
		setParameter(processor, vekt::mono::parameters::osc1Morph, 0.0f);
		setParameter(processor, vekt::mono::parameters::osc2Level, 0.0f);
		setParameter(processor, vekt::mono::parameters::osc3Level, 0.0f);
		setParameter(processor, vekt::mono::parameters::filterCutoff, 20'000.0f);
		setParameter(processor, vekt::mono::parameters::filterEnvelopeAmount, 0.0f);
		setParameter(processor, vekt::mono::parameters::filterVelocity, 0.0f);
		setParameter(processor, vekt::mono::parameters::filterKeyTracking, 0.0f);
		setParameter(processor, vekt::mono::parameters::filterResonance, 0.0f);
		setParameter(processor, vekt::mono::parameters::filterDrive, drive);
		processor.prepareToPlay(48'000.0, 4096);
		juce::AudioBuffer<float> buffer(2, 4096);
		juce::MidiBuffer noteOn;
		noteOn.addEvent(juce::MidiMessage::noteOn(1, 69, 1.0f), 0);
		renderBlock(processor, buffer, noteOn);
		renderBlock(processor, buffer);
		const auto fundamental = sinusoidMagnitude(buffer, 440.0f, 48'000.0f);
		const auto third = sinusoidMagnitude(buffer, 1'320.0f, 48'000.0f);
		return std::pair { third / fundamental, rms(buffer) };
	};
	const auto [cleanHarmonics, cleanRms] = render(0.0f);
	const auto [drivenHarmonics, drivenRms] = render(24.0f);
	REQUIRE(drivenHarmonics > cleanHarmonics * 2.0f);
	// Calibrated before the sine anchor was set to the saw's RMS (-1.76 dB); the saturated output barely
	// moves with input level, so compare against the clean level at the original sine amplitude.
	REQUIRE(drivenRms < cleanRms / static_cast<float>(vekt::mono::widthSineGain) * 4.0f);
}

TEST_CASE("Mono maximum resonance keeps floating-point peaks and obeys master trim", "[mono][processor][filter][headroom][slow]")
{
	for (const auto quality : { 0.0f, 1.0f })
		for (const auto drive : { 0.0f, 24.0f })
			for (const auto compensated : { false, true })
			{
				vekt::mono::PluginProcessor unity, trimmed;
				for (auto* processor : { &unity, &trimmed })
				{
					initializeDryVoice(*processor);
					setParameter(*processor, vekt::mono::parameters::quality, quality);
					setParameter(*processor, vekt::mono::parameters::osc1Level, 100.0f);
					setParameter(*processor, vekt::mono::parameters::filterCutoff, 1'000.0f);
					setParameter(*processor, vekt::mono::parameters::filterResonance, 100.0f);
					setParameter(*processor, vekt::mono::parameters::filterDrive, drive);
					setParameter(*processor, vekt::mono::parameters::filterQCompensation, compensated ? 1.0f : 0.0f);
				}
				setParameter(unity, vekt::mono::parameters::masterOutput, 0.0f);
				setParameter(trimmed, vekt::mono::parameters::masterOutput, -12.0f);
				unity.prepareToPlay(48'000.0, 1024);
				trimmed.prepareToPlay(48'000.0, 1024);
				juce::AudioBuffer<float> buffer(2, 1024), trimmedBuffer(2, 1024);
				juce::MidiBuffer note;
				note.addEvent(juce::MidiMessage::noteOn(1, 48, 1.0f), 0);
				float peak {};
				for (int block = 0; block < 48; ++block)
				{
					renderBlock(unity, buffer, note);
					renderBlock(trimmed, trimmedBuffer, note);
					note.clear();
					for (int channel = 0; channel < 2; ++channel)
						for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
						{
							const auto value = buffer.getSample(channel, sample);
							const auto trimmedValue = trimmedBuffer.getSample(channel, sample);
							REQUIRE(std::isfinite(value));
							REQUIRE(std::isfinite(trimmedValue));
							REQUIRE(trimmedValue == Catch::Approx(value * vekt::mono::dbToGain(-12.0f)).margin(2.0e-5f));
							peak = std::max(peak, std::abs(value));
						}
				}
				INFO("quality=" << quality << ", drive=" << drive << ", qCompensation=" << compensated << ", peak=" << peak);
				REQUIRE(peak > 0.01f);
				if (drive == 24.0f) REQUIRE(peak > 1.0f); // No hidden limiter at unity master.
				const auto work = unity.coupledWorkSnapshot();
				REQUIRE(work.unconverged == 0);
				REQUIRE(work.nonFinite == 0);
			}
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

TEST_CASE("Mono recalls project states without retired contour controls", "[mono][processor][contour][state]")
{
	vekt::mono::PluginProcessor source;
	juce::MemoryBlock data;
	source.getStateInformation(data);
	auto state = juce::ValueTree::readFromData(data.getData(), data.getSize());
	auto parameters = state.getChildWithName(source.getParameters().state.getType());
	parameters.removeChild(parameters.getChildWithProperty("id", vekt::mono::parameters::notePriority), nullptr);
	for (const auto* identifier : { "contourCurve", "releasePolicy" })
	{
		juce::ValueTree retired("PARAM");
		retired.setProperty("id", identifier, nullptr);
		retired.setProperty("value", 1, nullptr);
		parameters.addChild(retired, -1, nullptr);
	}
	juce::MemoryBlock legacy;
	juce::MemoryOutputStream stream(legacy, false);
	state.writeToStream(stream);
	vekt::mono::PluginProcessor restored;
	restored.setStateInformation(legacy.getData(), static_cast<int>(legacy.getSize()));
	REQUIRE(restored.getParameters().getRawParameterValue(vekt::mono::parameters::notePriority)->load() == Catch::Approx(0.0f));
	REQUIRE(restored.getParameters().getParameter("contourCurve") == nullptr);
	REQUIRE(restored.getParameters().getParameter("releasePolicy") == nullptr);
}

TEST_CASE("Mono rejects stored 16x quality instead of silently recalling 8x", "[mono][processor][state][quality]")
{
	vekt::mono::PluginProcessor source;
	juce::MemoryBlock data;
	source.getStateInformation(data);
	const auto original = juce::ValueTree::readFromData(data.getData(), data.getSize());
	REQUIRE(original.isValid());
	for (const auto legacyRoot : { false, true })
	for (int index = 0; index <= 4; ++index)
	{
		auto state = legacyRoot ? original.getChildWithName(source.getParameters().state.getType()).createCopy()
			: original.createCopy();
		if (legacyRoot) state.setProperty(vekt::state::StateManager::legacyVersionProperty, 3, nullptr);
		auto parameters = legacyRoot ? state : state.getChildWithName(source.getParameters().state.getType());
		auto quality = parameters.getChildWithProperty("id", vekt::mono::parameters::quality);
		REQUIRE(quality.isValid());
		quality.setProperty("value", index, nullptr);
		juce::MemoryBlock serialized;
		juce::MemoryOutputStream stream(serialized, false);
		state.writeToStream(stream);
		vekt::mono::PluginProcessor restored;
		setParameter(restored, vekt::mono::parameters::filterCutoff, 4'321.0f);
		setParameter(restored, vekt::mono::parameters::quality, 2.0f);
		restored.setStateInformation(serialized.getData(), static_cast<int>(serialized.getSize()));
		const auto* choice = dynamic_cast<juce::AudioParameterChoice*>(
			restored.getParameters().getParameter(vekt::mono::parameters::quality));
		REQUIRE(choice != nullptr);
		INFO("stored quality index=" << index << ", legacy root=" << legacyRoot);
		REQUIRE(choice->getIndex() == (index == 4 ? 2 : index));
		REQUIRE(restored.getParameters().getRawParameterValue(vekt::mono::parameters::filterCutoff)->load()
			== Catch::Approx(index == 4 ? 4'321.0f : 5'200.0f));
	}
}

TEST_CASE("Mono rejects obsolete pre-alpha project schemas without changing live state", "[mono][processor][state]")
{
	vekt::mono::PluginProcessor source;
	juce::MemoryBlock currentState;
	source.getStateInformation(currentState);
	for (const auto obsoleteSchema : { 1, 2 })
	{
		auto obsoleteState = juce::ValueTree::readFromData(currentState.getData(), currentState.getSize());
		REQUIRE(obsoleteState.isValid());
		obsoleteState.setProperty(vekt::state::StateManager::schemaVersionProperty, obsoleteSchema, nullptr);
		juce::MemoryBlock obsoleteData;
		juce::MemoryOutputStream stream(obsoleteData, false);
		obsoleteState.writeToStream(stream);

		vekt::mono::PluginProcessor restored;
		setParameter(restored, vekt::mono::parameters::filterCutoff, 4'321.0f);
		setParameter(restored, vekt::mono::parameters::heldKeyReturn, 0.0f);
		setParameter(restored, vekt::mono::parameters::filterQCompensation, 1.0f);
		restored.setStateInformation(obsoleteData.getData(), static_cast<int>(obsoleteData.getSize()));
		INFO("obsolete project schema=" << obsoleteSchema);
		REQUIRE(restored.getParameters().getRawParameterValue(vekt::mono::parameters::filterCutoff)->load() == Catch::Approx(4'321.0f));
		REQUIRE(restored.getParameters().getRawParameterValue(vekt::mono::parameters::heldKeyReturn)->load() == Catch::Approx(0.0f));
		REQUIRE(restored.getParameters().getRawParameterValue(vekt::mono::parameters::filterQCompensation)->load() == Catch::Approx(1.0f));
	}
}

TEST_CASE("Mono voice count changes cut active notes immediately", "[mono][processor]")
{
	vekt::mono::PluginProcessor processor;
	processor.prepareToPlay(48'000.0, 512);
	juce::AudioBuffer<float> buffer(2, 128);
	juce::MidiBuffer on;
	on.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
	renderBlock(processor, buffer, on);
	REQUIRE(buffer.getMagnitude(0, buffer.getNumSamples()) > 0.0f);
	setParameter(processor, vekt::mono::parameters::voiceCount, 4.0f);
	renderBlock(processor, buffer);
	REQUIRE(buffer.getMagnitude(0, buffer.getNumSamples()) == 0.0f);
	renderBlock(processor, buffer, on);
	REQUIRE(buffer.getMagnitude(0, buffer.getNumSamples()) > 0.0f);
}

TEST_CASE("Mono provides 26 categorized factory presets", "[mono][processor]")
{
	vekt::mono::PluginProcessor processor;
	auto& session = processor.getPresetSession();
	const auto& catalog = session.library();
	REQUIRE(catalog.factoryPresetCount() == 26);
	REQUIRE(catalog.folders(vekt::presets::PresetOrigin::factory).size() == 6);
	REQUIRE(processor.getNumPrograms() == 26);
	processor.setCurrentProgram(23);
	REQUIRE(processor.getCurrentProgram() == 23);
	REQUIRE(processor.getProgramName(23) == "Transmission FX");
}

TEST_CASE("Mono factory presets load with their stored values, including LFO settings", "[mono][processor][preset]")
{
	vekt::mono::PluginProcessor processor;
	const auto& catalog = processor.getPresetSession().library();
	int withLfoSettings {};
	for (std::size_t index = 0; index < catalog.factoryPresetCount(); ++index)
	{
		vekt::presets::Preset preset;
		REQUIRE(catalog.loadFactoryPreset(index, preset).wasOk());
		CAPTURE(preset.name);
		processor.setCurrentProgram(static_cast<int>(index));
		REQUIRE(processor.getCurrentProgram() == static_cast<int>(index));
		if (preset.soundSchemaVersion < 7) continue;
		++withLfoSettings;
		// Schema-7 and later files carry every sound parameter of their schema; each must exist and land unchanged.
		REQUIRE(preset.parameters.size() == vekt::mono::parameters::soundParameterIds.size()
			- (preset.soundSchemaVersion <= 7 ? vekt::mono::parameters::schema8ParameterIds.size() : 0)
			- (preset.soundSchemaVersion <= 9 ? vekt::mono::parameters::schema10ParameterIds.size() : 0)
			- (preset.soundSchemaVersion <= 11 ? vekt::mono::parameters::schema12ParameterIds.size() : 0));
		for (const auto& parameter : preset.parameters)
		{
			CAPTURE(parameter.identifier);
			const auto* value = processor.getParameters().getRawParameterValue(parameter.identifier);
			REQUIRE(value != nullptr);
			REQUIRE(value->load() == Catch::Approx(parameter.value).margin(1.0e-3));
		}
	}
	REQUIRE(withLfoSettings == 13);
}

TEST_CASE("Mono factory presets are not marked modified right after loading", "[mono][processor][preset]")
{
	// Non-zero continuous LFO depths come back a few 1e-6 off after the 0..1 parameter round trip; that
	// must not read as an edit (it used to show a "*" on every preset with LFO settings).
	vekt::mono::PluginProcessor processor;
	auto& session = processor.getPresetSession();
	for (std::size_t index = 0; index < session.library().factoryPresetCount(); ++index)
	{
		processor.setCurrentProgram(static_cast<int>(index));
		REQUIRE(session.loaded().has_value());
		CAPTURE(session.loaded()->name);
		REQUIRE_FALSE(session.modified());
	}
	// A real edit still counts.
	auto* depth = processor.getParameters().getParameter(vekt::mono::parameters::lfos[0].width[0]);
	depth->setValueNotifyingHost(depth->convertTo0to1(12.5f));
	REQUIRE(session.modified());
}

TEST_CASE("Mono Classic Three Bass uses three oscillators", "[mono][processor][preset]")
{
	vekt::mono::PluginProcessor processor;
	const auto& catalog = processor.getPresetSession().library();
	vekt::presets::Preset preset;
	bool classicPresetFound {};
	for (std::size_t index {}; index < catalog.factoryPresetCount(); ++index)
	{
		if (catalog.loadFactoryPreset(index, preset).wasOk() && preset.name == "Classic Three Bass")
		{
			classicPresetFound = true;
			break;
		}
	}
	REQUIRE(classicPresetFound);
	const auto value = [&preset](const char* identifier)
	{
		const auto found = std::find_if(preset.parameters.begin(), preset.parameters.end(), [identifier](const auto& parameter)
		{
			return parameter.identifier == identifier;
		});
		REQUIRE(found != preset.parameters.end());
		return found->value;
	};
	REQUIRE(value(vekt::mono::parameters::osc1Range) == Catch::Approx(0.0f));
	REQUIRE(value(vekt::mono::parameters::osc2Range) == Catch::Approx(1.0f));
	REQUIRE(value(vekt::mono::parameters::osc3Range) == Catch::Approx(1.0f));
	REQUIRE(value(vekt::mono::parameters::osc1Level) > 0.0f);
	REQUIRE(value(vekt::mono::parameters::osc2Level) > 0.0f);
	REQUIRE(value(vekt::mono::parameters::osc3Level) > 0.0f);
}

TEST_CASE("Mono factory presets use diverse oscillator and mixer designs", "[mono][processor][preset]")
{
	vekt::mono::PluginProcessor processor;
	const auto& catalog = processor.getPresetSession().library();
	std::set<juce::String> oscillatorShapes, oscillatorTunings;
	std::set<float> noiseLevels, voicePans;
	for (std::size_t index = 0; index < catalog.factoryPresetCount(); ++index)
	{
		vekt::presets::Preset preset;
		REQUIRE(catalog.loadFactoryPreset(index, preset).wasOk());
		// Presets voiced before the LFOs are schema 4; the ones given LFO settings are schema 7, or 9 with a filter Mode.
		REQUIRE((preset.soundSchemaVersion == 4 || preset.soundSchemaVersion == 7 || preset.soundSchemaVersion == 11));
		const auto value = [&preset](const char* identifier)
		{
			const auto found = std::find_if(preset.parameters.begin(), preset.parameters.end(), [identifier](const auto& parameter)
			{
				return parameter.identifier == identifier;
			});
			REQUIRE(found != preset.parameters.end());
			return found->value;
		};
		oscillatorShapes.insert(juce::String(value(vekt::mono::parameters::osc1Morph), 3) + "/"
			+ juce::String(value(vekt::mono::parameters::osc2Morph), 3) + "/"
			+ juce::String(value(vekt::mono::parameters::osc3Morph), 3) + ":"
			+ juce::String(value(vekt::mono::parameters::osc1PulseWidth), 2) + "/"
			+ juce::String(value(vekt::mono::parameters::osc2PulseWidth), 2) + "/"
			+ juce::String(value(vekt::mono::parameters::osc3PulseWidth), 2));
		oscillatorTunings.insert(juce::String(value(vekt::mono::parameters::osc1Octave), 0) + "/"
			+ juce::String(value(vekt::mono::parameters::osc2Octave), 0) + "/"
			+ juce::String(value(vekt::mono::parameters::osc3Octave), 0) + ":"
			+ juce::String(value(vekt::mono::parameters::osc1Fine), 1) + "/"
			+ juce::String(value(vekt::mono::parameters::osc2Fine), 1) + "/"
			+ juce::String(value(vekt::mono::parameters::osc3Fine), 1));
		noiseLevels.insert(value(vekt::mono::parameters::noiseLevel));
		voicePans.insert(value(vekt::mono::parameters::voiceWidth));
		REQUIRE(value(vekt::mono::parameters::filterQCompensation) == Catch::Approx(0.0f));
	}
	// Factory presets from before the filter Mode load as the plain LP ladder.
	for (std::size_t index = 0; index < catalog.factoryPresetCount(); ++index)
	{
		vekt::presets::Preset preset;
		REQUIRE(catalog.loadFactoryPreset(index, preset).wasOk());
		if (preset.soundSchemaVersion >= 8) continue;
		processor.setCurrentProgram(static_cast<int>(index));
		REQUIRE(processor.getParameters().getRawParameterValue(vekt::mono::parameters::filterMode)->load() == Catch::Approx(-1.0f));
	}
	REQUIRE(oscillatorShapes.size() >= 20);
	REQUIRE(oscillatorTunings.size() >= 20);
	REQUIRE(noiseLevels.size() >= 10);
	REQUIRE(voicePans.size() >= 10);
}

TEST_CASE("Mono rejects obsolete pre-alpha preset schemas without mutation", "[mono][processor][preset]")
{
	vekt::mono::PluginProcessor processor;
	setParameter(processor, vekt::mono::parameters::filterCutoff, 4'321.0f);
	vekt::presets::Preset current;
	REQUIRE(processor.getPresetSession().library().loadFactoryPreset(0, current).wasOk());
	for (const auto obsoleteSchema : { 1, 2, 3 })
	{
		auto obsolete = current;
		obsolete.soundSchemaVersion = obsoleteSchema;
		const auto parameterCount = obsolete.parameters.size();
		const auto result = processor.getPresetSession().prepare(obsolete);
		INFO("obsolete preset schema=" << obsoleteSchema);
		REQUIRE(result.failed());
		REQUIRE(obsolete.soundSchemaVersion == obsoleteSchema);
		REQUIRE(obsolete.parameters.size() == parameterCount);
		REQUIRE(processor.getParameters().getRawParameterValue(vekt::mono::parameters::filterCutoff)->load() == Catch::Approx(4'321.0f));
	}
	REQUIRE(processor.getPresetSession().prepare(current).wasOk());
}

TEST_CASE("Mono migrates schema 4 and 5 presets to analog independent ADSR", "[mono][processor][preset][contour]")
{
	vekt::mono::PluginProcessor processor;
	// A factory preset still stored at schema 4 (some were upgraded to 7 when they gained LFO settings).
	const auto& library = processor.getPresetSession().library();
	vekt::presets::Preset factory;
	for (std::size_t index = 0; index < library.factoryPresetCount(); ++index)
		if (library.loadFactoryPreset(index, factory).wasOk() && factory.soundSchemaVersion == 4) break;
	REQUIRE(factory.soundSchemaVersion == 4);
	for (const auto schema : { 4, 5 })
	{
		auto preset = factory;
		if (schema == 5)
		{
			preset.soundSchemaVersion = 5;
			preset.parameters.push_back({ vekt::mono::parameters::notePriority, 1.0f });
			preset.parameters.push_back({ "contourCurve", 0.0f });
			preset.parameters.push_back({ "releasePolicy", 2.0f });
		}
		const auto previousAmp = std::find_if(preset.parameters.begin(), preset.parameters.end(), [](const auto& p) { return p.identifier == vekt::mono::parameters::ampRelease; })->value;
		const auto previousFilter = std::find_if(preset.parameters.begin(), preset.parameters.end(), [](const auto& p) { return p.identifier == vekt::mono::parameters::filterRelease; })->value;
		REQUIRE(processor.getPresetSession().prepare(preset).wasOk());
		REQUIRE(preset.soundSchemaVersion == 12);
		REQUIRE(vekt::presets::PresetSchema::apply(preset, vekt::mono::parameters::presetProductIdentifier,
			processor.getParameters(), vekt::mono::parameters::soundParameterIds).wasOk());
		REQUIRE(processor.getParameters().getRawParameterValue(vekt::mono::parameters::ampRelease)->load() == Catch::Approx(previousAmp).margin(0.0001f));
		REQUIRE(processor.getParameters().getRawParameterValue(vekt::mono::parameters::filterRelease)->load() == Catch::Approx(previousFilter).margin(0.0001f));
		REQUIRE(processor.getParameters().getRawParameterValue(vekt::mono::parameters::notePriority)->load() == Catch::Approx(schema == 5 ? 1.0f : 0.0f));
		REQUIRE(std::none_of(preset.parameters.begin(), preset.parameters.end(), [](const auto& p)
		{
			return p.identifier == "contourCurve" || p.identifier == "releasePolicy";
		}));
	}
}

TEST_CASE("Mono preset changes stop voices from the previous patch", "[mono][processor][preset]")
{
	vekt::mono::PluginProcessor processor;
	setParameter(processor, vekt::mono::parameters::ampRelease, 20.0f);
	processor.prepareToPlay(48'000.0, 128);
	juce::AudioBuffer<float> buffer(2, 128);
	juce::MidiBuffer noteOn;
	noteOn.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
	processor.processBlock(buffer, noteOn);
	REQUIRE(buffer.getMagnitude(0, 0, buffer.getNumSamples()) > 0.0f);

	REQUIRE(processor.loadNextPreset().wasOk());
	juce::MidiBuffer empty;
	processor.processBlock(buffer, empty);
	REQUIRE(buffer.getMagnitude(0, 0, buffer.getNumSamples()) == Catch::Approx(0.0f).margin(1.0e-7f));
	REQUIRE(buffer.getMagnitude(1, 0, buffer.getNumSamples()) == Catch::Approx(0.0f).margin(1.0e-7f));
}

TEST_CASE("Mono held-key return is configurable", "[mono][processor][midi]")
{
	for (const auto mode : { 1.0f, 2.0f })
		for (const auto heldKeyReturn : { 0.0f, 1.0f })
	{
		vekt::mono::PluginProcessor processor;
		setParameter(processor, vekt::mono::parameters::performanceMode, mode);
		setParameter(processor, vekt::mono::parameters::heldKeyReturn, heldKeyReturn);
		setParameter(processor, vekt::mono::parameters::ampRelease, 0.005f);
		processor.prepareToPlay(48'000.0, 1'024);
		juce::AudioBuffer<float> buffer(2, 1'024);
		juce::MidiBuffer midi;
		midi.addEvent(juce::MidiMessage::noteOn(1, 48, 0.9f), 0);
		midi.addEvent(juce::MidiMessage::noteOn(1, 72, 0.9f), 128);
		midi.addEvent(juce::MidiMessage::noteOff(1, 72), 256);
		processor.processBlock(buffer, midi);
		float postReleaseEnergy {};
		for (int sample = 768; sample < buffer.getNumSamples(); ++sample)
			postReleaseEnergy += std::abs(buffer.getSample(0, sample));
		if (heldKeyReturn > 0.5f) REQUIRE(postReleaseEnergy > 0.01f);
		else REQUIRE(postReleaseEnergy < 1.0e-4f);
	}
}

TEST_CASE("Mono active-note transitions preserve the sample boundary", "[mono][processor][midi][declick]")
{
	for (const auto mode : { 1.0f, 2.0f })
	{
		vekt::mono::PluginProcessor processor;
		setParameter(processor, vekt::mono::parameters::performanceMode, mode);
		setParameter(processor, vekt::mono::parameters::osc1Morph, 2.0f);
		setParameter(processor, vekt::mono::parameters::osc2Level, 0.0f);
		setParameter(processor, vekt::mono::parameters::osc3Level, 0.0f);
		setParameter(processor, vekt::mono::parameters::filterCutoff, 12'000.0f);
		setParameter(processor, vekt::mono::parameters::filterEnvelopeAmount, 0.0f);
		setParameter(processor, vekt::mono::parameters::ampAttack, 0.0005f);
		// Sample-exact boundary check: at 1x no resampling filter smears the voice's own crossfade.
		setParameter(processor, vekt::mono::parameters::quality, 0.0f);
		processor.prepareToPlay(48'000.0, 1'024);
		juce::AudioBuffer<float> buffer(2, 1'024);
		juce::MidiBuffer midi;
		midi.addEvent(juce::MidiMessage::noteOn(1, 48, 1.0f), 0);
		midi.addEvent(juce::MidiMessage::noteOn(1, 72, 0.35f), 512);
		processor.processBlock(buffer, midi);
		for (int channel = 0; channel < 2; ++channel)
			REQUIRE(buffer.getSample(channel, 512) == Catch::Approx(buffer.getSample(channel, 511)).margin(1.0e-5f));
	}
}

TEST_CASE("Mono active-note release preserves the sample boundary", "[mono][processor][midi][declick]")
{
	vekt::mono::PluginProcessor processor;
	setParameter(processor, vekt::mono::parameters::performanceMode, 2.0f);
	setParameter(processor, vekt::mono::parameters::osc1Morph, 3.0f);
	setParameter(processor, vekt::mono::parameters::osc2Level, 0.0f);
	setParameter(processor, vekt::mono::parameters::osc3Level, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterCutoff, 12'000.0f);
	setParameter(processor, vekt::mono::parameters::filterEnvelopeAmount, 0.0f);
	processor.prepareToPlay(48'000.0, 1'024);
	juce::AudioBuffer<float> buffer(2, 1'024);
	juce::MidiBuffer midi;
	midi.addEvent(juce::MidiMessage::noteOn(1, 48, 1.0f), 0);
	midi.addEvent(juce::MidiMessage::noteOn(1, 72, 1.0f), 384);
	midi.addEvent(juce::MidiMessage::noteOff(1, 72), 768);
	processor.processBlock(buffer, midi);
	for (int channel = 0; channel < 2; ++channel)
	{
		float nearbyMaximumDelta {};
		for (int sample = 704; sample < 768; ++sample)
			nearbyMaximumDelta = std::max(nearbyMaximumDelta,
				std::abs(buffer.getSample(channel, sample) - buffer.getSample(channel, sample - 1)));
		const auto boundaryDelta = std::abs(buffer.getSample(channel, 768) - buffer.getSample(channel, 767));
		REQUIRE(boundaryDelta <= nearbyMaximumDelta * 1.1f + 1.0e-5f);
	}
}

TEST_CASE("Mono poly repeated keys retrigger their own voice instead of stacking", "[mono][processor][midi]")
{
	const auto prepared = []
	{
		auto processor = std::make_unique<vekt::mono::PluginProcessor>();
		setParameter(*processor, vekt::mono::parameters::performanceMode, 0.0f);
		setParameter(*processor, vekt::mono::parameters::ampRelease, 5.0f);
		processor->prepareToPlay(48'000.0, 256);
		return processor;
	};
	juce::AudioBuffer<float> buffer(2, 256);
	const auto play = [&buffer](vekt::mono::PluginProcessor& processor, std::initializer_list<juce::MidiMessage> messages)
	{
		juce::MidiBuffer midi;
		for (const auto& message : messages) midi.addEvent(message, 0);
		renderBlock(processor, buffer, midi);
	};

	SECTION("a key pressed again during its release tail")
	{
		auto processor = prepared();
		play(*processor, { juce::MidiMessage::noteOn(1, 60, 0.8f) });
		play(*processor, { juce::MidiMessage::noteOff(1, 60) });
		play(*processor, { juce::MidiMessage::noteOn(1, 60, 0.8f) });
		REQUIRE(processor->getSoundingVoiceCount() == 1);
		// The retriggered voice is held again: one note-off starts its release rather than leaving a stuck copy.
		play(*processor, { juce::MidiMessage::noteOff(1, 60) });
		REQUIRE(processor->getSoundingVoiceCount() == 1);
	}
	SECTION("a key pressed twice without a note-off")
	{
		auto processor = prepared();
		play(*processor, { juce::MidiMessage::noteOn(1, 60, 0.8f) });
		play(*processor, { juce::MidiMessage::noteOn(1, 60, 0.5f) });
		REQUIRE(processor->getSoundingVoiceCount() == 1);
	}
	SECTION("a key held by the sustain pedal")
	{
		auto processor = prepared();
		play(*processor, { juce::MidiMessage::controllerEvent(1, 64, 127), juce::MidiMessage::noteOn(1, 60, 0.8f) });
		play(*processor, { juce::MidiMessage::noteOff(1, 60) });
		play(*processor, { juce::MidiMessage::noteOn(1, 60, 0.8f) });
		REQUIRE(processor->getSoundingVoiceCount() == 1);
	}
	SECTION("the same key on another channel, and other keys, still get their own voices")
	{
		auto processor = prepared();
		play(*processor, { juce::MidiMessage::noteOn(1, 60, 0.8f) });
		play(*processor, { juce::MidiMessage::noteOn(2, 60, 0.8f) });
		play(*processor, { juce::MidiMessage::noteOn(1, 64, 0.8f) });
		REQUIRE(processor->getSoundingVoiceCount() == 3);
	}
}

TEST_CASE("Mono poly repeated-key retrigger avoids an exceptional sample-boundary jump", "[mono][processor][midi][declick]")
{
	vekt::mono::PluginProcessor processor;
	setParameter(processor, vekt::mono::parameters::performanceMode, 0.0f);
	setParameter(processor, vekt::mono::parameters::voiceWidth, 100.0f);
	setParameter(processor, vekt::mono::parameters::osc1Morph, 2.0f);
	setParameter(processor, vekt::mono::parameters::osc2Level, 0.0f);
	setParameter(processor, vekt::mono::parameters::osc3Level, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterCutoff, 12'000.0f);
	setParameter(processor, vekt::mono::parameters::filterEnvelopeAmount, 0.0f);
	setParameter(processor, vekt::mono::parameters::ampAttack, 0.0005f);
	setParameter(processor, vekt::mono::parameters::ampRelease, 2.0f);
	processor.prepareToPlay(48'000.0, 1'024);
	juce::AudioBuffer<float> buffer(2, 1'024);
	juce::MidiBuffer midi;
	midi.addEvent(juce::MidiMessage::noteOn(1, 48, 1.0f), 0);
	midi.addEvent(juce::MidiMessage::noteOff(1, 48), 384);
	constexpr auto retriggerSample = 768;
	midi.addEvent(juce::MidiMessage::noteOn(1, 48, 0.25f), retriggerSample);
	processor.processBlock(buffer, midi);
	REQUIRE(processor.getSoundingVoiceCount() == 1);
	for (int channel = 0; channel < 2; ++channel)
	{
		float nearbyMaximumDelta {};
		for (int sample = retriggerSample - 64; sample < retriggerSample; ++sample)
			nearbyMaximumDelta = std::max(nearbyMaximumDelta,
				std::abs(buffer.getSample(channel, sample) - buffer.getSample(channel, sample - 1)));
		const auto boundaryDelta = std::abs(buffer.getSample(channel, retriggerSample) - buffer.getSample(channel, retriggerSample - 1));
		REQUIRE(boundaryDelta <= nearbyMaximumDelta * 1.1f + 1.0e-5f);
	}
}

TEST_CASE("Mono voice stealing avoids an exceptional sample-boundary jump", "[mono][processor][midi][declick]")
{
	vekt::mono::PluginProcessor processor;
	setParameter(processor, vekt::mono::parameters::performanceMode, 0.0f);
	setParameter(processor, vekt::mono::parameters::voiceCount, 2.0f);
	setParameter(processor, vekt::mono::parameters::voiceWidth, 100.0f);
	setParameter(processor, vekt::mono::parameters::osc1Morph, 2.0f);
	setParameter(processor, vekt::mono::parameters::osc2Level, 0.0f);
	setParameter(processor, vekt::mono::parameters::osc3Level, 0.0f);
	setParameter(processor, vekt::mono::parameters::filterCutoff, 12'000.0f);
	setParameter(processor, vekt::mono::parameters::filterEnvelopeAmount, 0.0f);
	setParameter(processor, vekt::mono::parameters::ampAttack, 0.0005f);
	processor.prepareToPlay(48'000.0, 1'024);
	juce::AudioBuffer<float> buffer(2, 1'024);
	juce::MidiBuffer midi;
	for (int voice = 0; voice < 8; ++voice)
		midi.addEvent(juce::MidiMessage::noteOn(1, 48 + voice * 2, 1.0f), voice * 32);
	constexpr auto stealSample = 768;
	midi.addEvent(juce::MidiMessage::noteOn(1, 84, 0.25f), stealSample);
	processor.processBlock(buffer, midi);

	for (int channel = 0; channel < 2; ++channel)
	{
		float nearbyMaximumDelta {};
		for (int sample = stealSample - 64; sample < stealSample; ++sample)
			nearbyMaximumDelta = std::max(nearbyMaximumDelta,
				std::abs(buffer.getSample(channel, sample) - buffer.getSample(channel, sample - 1)));
		const auto boundaryDelta = std::abs(buffer.getSample(channel, stealSample) - buffer.getSample(channel, stealSample - 1));
		REQUIRE(boundaryDelta <= nearbyMaximumDelta * 1.1f + 1.0e-5f);
	}
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

TEST_CASE("Mono quality choices activate distinct oversampling paths and latency", "[mono][processor][quality]")
{
	vekt::dsp::OversamplingBank<float> expected(2);
	expected.prepare(257);
	const std::array paths {
		vekt::dsp::OversamplingQuality { vekt::dsp::OversamplingFactor::off, vekt::dsp::OversamplingFilter::polyphaseIIR },
		vekt::dsp::OversamplingQuality { vekt::dsp::OversamplingFactor::x2, vekt::dsp::OversamplingFilter::polyphaseIIR },
		vekt::dsp::OversamplingQuality { vekt::dsp::OversamplingFactor::x4, vekt::dsp::OversamplingFilter::polyphaseFIR },
		vekt::dsp::OversamplingQuality { vekt::dsp::OversamplingFactor::x8, vekt::dsp::OversamplingFilter::polyphaseFIR }
	};
	for (std::size_t index = 0; index < paths.size(); ++index)
	{
		vekt::mono::PluginProcessor processor;
		setParameter(processor, vekt::mono::parameters::quality, static_cast<float>(index));
		processor.prepareToPlay(48'000.0, 257);
		expected.activate(paths[index]);
		CAPTURE(index, expected.getActiveFactor(), expected.getActiveLatencySamples());
		REQUIRE(processor.getActiveQuality() == static_cast<int>(index));
		REQUIRE(processor.getLatencySamples() == expected.getActiveLatencySamples());
		juce::AudioBuffer<float> buffer(2, 257);
		juce::MidiBuffer midi;
		midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
		midi.addEvent(juce::MidiMessage::noteOff(1, 60), 64);
		midi.addEvent(juce::MidiMessage::noteOn(1, 67, 0.9f), 128);
		processor.processBlock(buffer, midi);
		float outputEnergy {};
		for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
			for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
			{
				const auto value = buffer.getSample(channel, sample);
				REQUIRE(std::isfinite(value));
				outputEnergy += std::abs(value);
			}
		REQUIRE(outputEnergy > 1.0e-4f);
	}
}

TEST_CASE("Mono unison spread changes stereo rendering", "[mono][processor][unison]")
{
	vekt::mono::PluginProcessor centered, spread;
	for (auto* processor : { &centered, &spread })
	{
		setParameter(*processor, vekt::mono::parameters::performanceMode, 1.0f);
		setParameter(*processor, vekt::mono::parameters::unison, 2.0f);
		setParameter(*processor, vekt::mono::parameters::unisonDetune, 20.0f);
		setParameter(*processor, vekt::mono::parameters::unisonSpread, 0.0f);
		setParameter(*processor, vekt::mono::parameters::voiceWidth, 0.0f);
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

TEST_CASE("Mono voice pan controls round-robin stereo mix", "[mono][processor][stereo]")
{
	vekt::mono::PluginProcessor centered, panned;
	for (auto* processor : { &centered, &panned })
	{
		setParameter(*processor, vekt::mono::parameters::performanceMode, 0.0f);
		setParameter(*processor, vekt::mono::parameters::unison, 0.0f);
		setParameter(*processor, vekt::mono::parameters::unisonSpread, 0.0f);
		setParameter(*processor, vekt::mono::parameters::osc2Level, 0.0f);
		setParameter(*processor, vekt::mono::parameters::osc3Level, 0.0f);
		processor->prepareToPlay(48'000.0, 512);
	}
	setParameter(centered, vekt::mono::parameters::voiceWidth, 0.0f);
	setParameter(panned, vekt::mono::parameters::voiceWidth, 100.0f);
	juce::AudioBuffer<float> centeredBuffer(2, 512), pannedBuffer(2, 512);
	juce::MidiBuffer centeredMidi, pannedMidi;
	centeredMidi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
	pannedMidi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
	centered.processBlock(centeredBuffer, centeredMidi);
	panned.processBlock(pannedBuffer, pannedMidi);

	float centeredDifference {}, pannedDifference {};
	for (int sample = 0; sample < centeredBuffer.getNumSamples(); ++sample)
	{
		centeredDifference += std::abs(centeredBuffer.getSample(0, sample) - centeredBuffer.getSample(1, sample));
		pannedDifference += std::abs(pannedBuffer.getSample(0, sample) - pannedBuffer.getSample(1, sample));
	}
	REQUIRE(centeredDifference == Catch::Approx(0.0f).margin(1.0e-6f));
	REQUIRE(pannedDifference > 0.01f);
	// A mono voice changes channel balance, not its summed stereo power.
	const auto centeredPower = stereoPowerRms(centeredBuffer);
	const auto pannedPower = stereoPowerRms(pannedBuffer);
	INFO("centered stereo power RMS=" << centeredPower << ", panned=" << pannedPower);
	REQUIRE(centeredPower > 0.001f);
	REQUIRE(pannedPower == Catch::Approx(centeredPower).epsilon(0.01f));
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

TEST_CASE("Mono processor and extracted voice render identically", "[mono][processor][engine][determinism]")
{
	vekt::mono::PluginProcessor processor;
	for (auto* parameter : processor.juce::AudioProcessor::getParameters())
		parameter->setValueNotifyingHost(parameter->getDefaultValue());

	vekt::mono::MonoVoiceSettings settings {
		.range = { 2.0f, 1.0f, 3.0f },
		.semitone = { 0.0f, 7.0f, -12.0f },
		.fine = { 3.0f, -4.0f, 7.0f },
		.octave = { 0.0f, 0.0f, 0.0f },
		.level = { 0.35f, 0.2f, 0.15f },
		.morph = { 2.0f, 1.0f, 3.0f },
		.pulseWidth = { 50.0f, 45.0f, 60.0f },
		.noiseLevel = 0.1f,
		.cutoff = 1'200.0f,
		.resonance = 0.55f,
		.tracking = 0.4f,
		.envelopeAmount = 0.35f,
		.drive = 6.0f,
		.ampAttack = 0.002f,
		.ampDecay = 0.12f,
		.ampSustain = 0.72f,
		.ampRelease = 0.3f,
		.filterAttack = 0.004f,
		.filterDecay = 0.18f,
		.filterSustain = 0.45f,
		.filterRelease = 0.25f,
		.ampVelocity = 0.4f,
		.filterVelocity = 0.3f,
		.calibration = 3.0f,
		.detune = 9.0f,
		.unisonSpread = 0.35f,
		.voiceWidth = 0.0f,
		.glideTime = 0.0f,
		.drift = 25.0f,
		.unison = 2,
		.glideMode = 0,
		.noiseType = 1,
		.qCompensation = true
	};

	const std::array ranges { vekt::mono::parameters::osc1Range, vekt::mono::parameters::osc2Range, vekt::mono::parameters::osc3Range };
	const std::array semitones { vekt::mono::parameters::osc1Semitone, vekt::mono::parameters::osc2Semitone, vekt::mono::parameters::osc3Semitone };
	const std::array fines { vekt::mono::parameters::osc1Fine, vekt::mono::parameters::osc2Fine, vekt::mono::parameters::osc3Fine };
	const std::array octaves { vekt::mono::parameters::osc1Octave, vekt::mono::parameters::osc2Octave, vekt::mono::parameters::osc3Octave };
	const std::array levels { vekt::mono::parameters::osc1Level, vekt::mono::parameters::osc2Level, vekt::mono::parameters::osc3Level };
	const std::array morphs { vekt::mono::parameters::osc1Morph, vekt::mono::parameters::osc2Morph, vekt::mono::parameters::osc3Morph };
	const std::array widths { vekt::mono::parameters::osc1PulseWidth, vekt::mono::parameters::osc2PulseWidth, vekt::mono::parameters::osc3PulseWidth };
	for (std::size_t oscillator = 0; oscillator < 3; ++oscillator)
	{
		setParameter(processor, ranges[oscillator], settings.range[oscillator]);
		setParameter(processor, semitones[oscillator], settings.semitone[oscillator]);
		setParameter(processor, fines[oscillator], settings.fine[oscillator]);
		setParameter(processor, octaves[oscillator], settings.octave[oscillator]);
		setParameter(processor, levels[oscillator], settings.level[oscillator] * 100.0f);
		setParameter(processor, morphs[oscillator], settings.morph[oscillator]);
		setParameter(processor, widths[oscillator], settings.pulseWidth[oscillator]);
	}
	setParameter(processor, vekt::mono::parameters::performanceMode, 1.0f);
	setParameter(processor, vekt::mono::parameters::quality, 0.0f);
	setParameter(processor, vekt::mono::parameters::unison, 1.0f);
	setParameter(processor, vekt::mono::parameters::unisonDetune, settings.detune);
	setParameter(processor, vekt::mono::parameters::unisonSpread, settings.unisonSpread * 100.0f);
	setParameter(processor, vekt::mono::parameters::voiceWidth, settings.voiceWidth * 100.0f);
	setParameter(processor, vekt::mono::parameters::glideMode, static_cast<float>(settings.glideMode));
	setParameter(processor, vekt::mono::parameters::glideTime, settings.glideTime);
	setParameter(processor, vekt::mono::parameters::calibration, settings.calibration);
	setParameter(processor, vekt::mono::parameters::drift, settings.drift);
	setParameter(processor, vekt::mono::parameters::masterOutput, 0.0f);
	setParameter(processor, vekt::mono::parameters::noiseType, static_cast<float>(settings.noiseType));
	setParameter(processor, vekt::mono::parameters::noiseLevel, settings.noiseLevel * 100.0f);
	setParameter(processor, vekt::mono::parameters::filterCutoff, settings.cutoff);
	setParameter(processor, vekt::mono::parameters::filterResonance, settings.resonance * 100.0f);
	setParameter(processor, vekt::mono::parameters::filterKeyTracking, settings.tracking * 100.0f);
	setParameter(processor, vekt::mono::parameters::filterEnvelopeAmount, settings.envelopeAmount * 100.0f);
	setParameter(processor, vekt::mono::parameters::filterDrive, settings.drive);
	setParameter(processor, vekt::mono::parameters::filterQCompensation, 1.0f);
	setParameter(processor, vekt::mono::parameters::ampAttack, settings.ampAttack);
	setParameter(processor, vekt::mono::parameters::ampDecay, settings.ampDecay);
	setParameter(processor, vekt::mono::parameters::ampSustain, settings.ampSustain * 100.0f);
	setParameter(processor, vekt::mono::parameters::ampRelease, settings.ampRelease);
	setParameter(processor, vekt::mono::parameters::filterAttack, settings.filterAttack);
	setParameter(processor, vekt::mono::parameters::filterDecay, settings.filterDecay);
	setParameter(processor, vekt::mono::parameters::filterSustain, settings.filterSustain * 100.0f);
	setParameter(processor, vekt::mono::parameters::filterRelease, settings.filterRelease);
	setParameter(processor, vekt::mono::parameters::ampVelocity, settings.ampVelocity * 100.0f);
	setParameter(processor, vekt::mono::parameters::filterVelocity, settings.filterVelocity * 100.0f);

	const auto raw = [&processor](const char* identifier) { return processor.getParameters().getRawParameterValue(identifier)->load(); };
	for (std::size_t oscillator = 0; oscillator < 3; ++oscillator)
	{
		settings.range[oscillator] = raw(ranges[oscillator]);
		settings.semitone[oscillator] = raw(semitones[oscillator]);
		settings.fine[oscillator] = raw(fines[oscillator]);
		settings.octave[oscillator] = raw(octaves[oscillator]);
		settings.level[oscillator] = raw(levels[oscillator]) * 0.01f;
		settings.morph[oscillator] = raw(morphs[oscillator]);
		settings.pulseWidth[oscillator] = raw(widths[oscillator]);
	}
	settings.noiseType = juce::roundToInt(raw(vekt::mono::parameters::noiseType));
	settings.noiseLevel = raw(vekt::mono::parameters::noiseLevel) * 0.01f;
	settings.cutoff = raw(vekt::mono::parameters::filterCutoff);
	settings.resonance = raw(vekt::mono::parameters::filterResonance) * 0.01f;
	settings.tracking = raw(vekt::mono::parameters::filterKeyTracking) * 0.01f;
	settings.envelopeAmount = raw(vekt::mono::parameters::filterEnvelopeAmount) * 0.01f;
	settings.drive = raw(vekt::mono::parameters::filterDrive);
	settings.qCompensation = raw(vekt::mono::parameters::filterQCompensation) >= 0.5f;
	settings.filterMode = raw(vekt::mono::parameters::filterMode);
	settings.ampAttack = raw(vekt::mono::parameters::ampAttack);
	settings.ampDecay = raw(vekt::mono::parameters::ampDecay);
	settings.ampSustain = raw(vekt::mono::parameters::ampSustain) * 0.01f;
	settings.ampRelease = raw(vekt::mono::parameters::ampRelease);
	settings.filterAttack = raw(vekt::mono::parameters::filterAttack);
	settings.filterDecay = raw(vekt::mono::parameters::filterDecay);
	settings.filterSustain = raw(vekt::mono::parameters::filterSustain) * 0.01f;
	settings.filterRelease = raw(vekt::mono::parameters::filterRelease);
	settings.ampVelocity = raw(vekt::mono::parameters::ampVelocity) * 0.01f;
	settings.filterVelocity = raw(vekt::mono::parameters::filterVelocity) * 0.01f;
	settings.calibration = raw(vekt::mono::parameters::calibration);
	settings.unison = raw(vekt::mono::parameters::unison) < 0.5f ? 1 : raw(vekt::mono::parameters::unison) < 1.5f ? 2 : 4;
	settings.detune = raw(vekt::mono::parameters::unisonDetune);
	settings.unisonSpread = raw(vekt::mono::parameters::unisonSpread) * 0.01f;
	settings.voiceWidth = raw(vekt::mono::parameters::voiceWidth) * 0.01f;
	settings.drift = raw(vekt::mono::parameters::drift);
	settings.glideMode = juce::roundToInt(raw(vekt::mono::parameters::glideMode));
	settings.glideTime = raw(vekt::mono::parameters::glideTime);

	constexpr auto sampleRate = 48'000.0;
	constexpr auto blockSize = 512;
	constexpr auto note = 64;
	const auto noteOn = juce::MidiMessage::noteOn(1, note, 0.73f);
	const auto velocity = noteOn.getFloatVelocity();
	processor.prepareToPlay(sampleRate, blockSize);
	juce::AudioBuffer<float> processorBuffer(2, blockSize);
	juce::MidiBuffer midi;
	midi.addEvent(noteOn, 0);
	processor.processBlock(processorBuffer, midi);

	vekt::mono::MonoVoice voice;
	voice.prepare(sampleRate, 0x4d6f6e6fu);
	voice.setPanPosition(0.0f);
	voice.start(1, note, velocity, settings, true, false, 1);
	for (int sample = 0; sample < blockSize; ++sample)
	{
		float left {}, right {};
		voice.render(left, right, settings, 0.0f);
		REQUIRE(processorBuffer.getSample(0, sample) == Catch::Approx(left).margin(1.0e-7f));
		REQUIRE(processorBuffer.getSample(1, sample) == Catch::Approx(right).margin(1.0e-7f));
	}
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
	// MIDI logic only: at 1x the output is exactly silent after All Sound Off (no decimator tail).
	setParameter(processor, vekt::mono::parameters::quality, 0.0f);
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

TEST_CASE("Mono Multicore renders exactly the same samples as single-threaded rendering", "[mono][processor][multicore]")
{
	// Work units and their summing order do not depend on threads, so switching Multicore must not change a
	// single sample: 16 voices, every unison setting, 1x and 2x, notes ending inside blocks and LFO movement.
	for (const auto unison : { 0.0f, 1.0f, 2.0f })
		for (const auto quality : { 0.0f, 1.0f })
		{
			CAPTURE(unison, quality);
			vekt::mono::PluginProcessor single, multi;
			for (auto* processor : { &single, &multi })
			{
				setParameter(*processor, vekt::mono::parameters::voiceCount, 4.0f); // 16 voices
				setParameter(*processor, vekt::mono::parameters::unison, unison);
				setParameter(*processor, vekt::mono::parameters::quality, quality);
				setParameter(*processor, vekt::mono::parameters::ampRelease, 0.02f);
				setParameter(*processor, vekt::mono::parameters::lfos[0].width[0], 30.0f);
				setParameter(*processor, vekt::mono::parameters::lfos[0].rate, 7.0f);
			}
			setParameter(multi, vekt::mono::parameters::multicore, 1.0f);
			single.prepareToPlay(48'000.0, 256);
			multi.prepareToPlay(48'000.0, 256);
			REQUIRE(single.getRenderHelperCount() == 0);
			REQUIRE(multi.getRenderHelperCount() == vekt::mono::MonoRenderWorkers::defaultThreadCount());
			juce::AudioBuffer<float> a(2, 256), b(2, 256);
			for (int block = 0; block < 40; ++block)
			{
				juce::MidiBuffer midi;
				if (block < 16) midi.addEvent(juce::MidiMessage::noteOn(1, 40 + 3 * block, 0.8f), (block * 37) % 256);
				if (block >= 20 && block < 36) midi.addEvent(juce::MidiMessage::noteOff(1, 40 + 3 * (block - 20)), (block * 53) % 256);
				auto midiCopy = midi;
				single.processBlock(a, midi);
				multi.processBlock(b, midiCopy);
				for (int channel = 0; channel < 2; ++channel)
					for (int sample = 0; sample < a.getNumSamples(); ++sample)
					{
						CAPTURE(block, channel, sample);
						REQUIRE(std::bit_cast<std::uint32_t>(a.getSample(channel, sample)) == std::bit_cast<std::uint32_t>(b.getSample(channel, sample)));
					}
			}
		}
}

TEST_CASE("Mono Multicore helpers are created once when prepare and enabling overlap", "[mono][processor][multicore]")
{
	// prepareToPlay (a host thread) and switching Multicore on (the message thread) can both create the pool.
	juce::ScopedJuceInitialiser_GUI messageThread;
	for (int attempt = 0; attempt < 20; ++attempt)
	{
		vekt::mono::PluginProcessor processor;
		std::atomic<bool> go {};
		std::thread host([&]
		{
			while (!go.load()) std::this_thread::yield();
			processor.prepareToPlay(48'000.0, 128);
		});
		go.store(true);
		setParameter(processor, vekt::mono::parameters::multicore, 1.0f);
		host.join();
		processor.prepareToPlay(48'000.0, 128); // creates the pool if the host thread saw Multicore still off
		REQUIRE(processor.getRenderHelperCount() == vekt::mono::MonoRenderWorkers::defaultThreadCount());
	}
}

TEST_CASE("Mono Multicore keeps rendering while the host workgroup changes", "[mono][processor][multicore]")
{
	// The workgroup arrives on the render thread and must never wait for a helper; rendering continues and
	// still matches single-threaded output.
	vekt::mono::PluginProcessor single, multi;
	for (auto* processor : { &single, &multi })
		setParameter(*processor, vekt::mono::parameters::voiceCount, 4.0f);
	setParameter(multi, vekt::mono::parameters::multicore, 1.0f);
	single.prepareToPlay(48'000.0, 128);
	multi.prepareToPlay(48'000.0, 128);
	REQUIRE(multi.getRenderHelperCount() > 0);
	juce::AudioBuffer<float> a(2, 128), b(2, 128);
	for (int block = 0; block < 60; ++block)
	{
		juce::MidiBuffer midi;
		if (block < 12) midi.addEvent(juce::MidiMessage::noteOn(1, 48 + 2 * block, 0.8f), 0);
		auto midiCopy = midi;
		multi.audioWorkgroupContextChanged({}); // as JUCE does from the render callback
		single.processBlock(a, midi);
		multi.processBlock(b, midiCopy);
		for (int channel = 0; channel < 2; ++channel)
			for (int sample = 0; sample < a.getNumSamples(); ++sample)
				REQUIRE(std::bit_cast<std::uint32_t>(a.getSample(channel, sample)) == std::bit_cast<std::uint32_t>(b.getSample(channel, sample)));
	}
}
