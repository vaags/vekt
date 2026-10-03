#include "KobberQualitySweep.h"
#include <vekt/kobber/PluginProcessor.h>

#include "KobberVoice.h"
#include "KobberParameterChoices.h"
#include "KobberSettingsSnapshot.h"
#include "ContourEnvelope.h"
#include "KobberRenderWorkers.h"

#include <vekt/audio_analysis/Measurements.h>
#include <vekt/presets/PresetSchema.h>

#include <juce_events/juce_events.h>

#include <catch2/catch_approx.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <numbers>
#include <memory>
#include <set>
#include <thread>
#include <tuple>
#include <vector>

namespace
{
void setParameter(vekt::kobber::PluginProcessor& processor, const char* identifier, float value)
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

void renderBlock(vekt::kobber::PluginProcessor& processor, juce::AudioBuffer<float>& buffer, juce::MidiBuffer midi = {})
{
	buffer.clear();
	processor.processBlock(buffer, midi);
}

void initializeDryVoice(vekt::kobber::PluginProcessor& processor)
{
	for (auto* parameter : processor.juce::AudioProcessor::getParameters())
		parameter->setValueNotifyingHost(parameter->getDefaultValue());
	setParameter(processor, vekt::kobber::parameters::performanceMode, 1.0f);
	setParameter(processor, vekt::kobber::parameters::osc1Morph, 0.0f);
	setParameter(processor, vekt::kobber::parameters::osc1Level, 20.0f);
	setParameter(processor, vekt::kobber::parameters::filterCutoff, 20'000.0f);
	setParameter(processor, vekt::kobber::parameters::filterResonance, 0.0f);
	setParameter(processor, vekt::kobber::parameters::filterKeyTracking, 0.0f);
	setParameter(processor, vekt::kobber::parameters::filterEnvelopeAmount, 0.0f);
	setParameter(processor, vekt::kobber::parameters::filterVelocity, 0.0f);
	setParameter(processor, vekt::kobber::parameters::ampAttack, 0.0005f);
	setParameter(processor, vekt::kobber::parameters::ampSustain, 100.0f);
	setParameter(processor, vekt::kobber::parameters::ampVelocity, 0.0f);
}
}

TEST_CASE("Kobber stereo power RMS preserves mono power across equal-power pan and phase", "[kobber][processor][stereo]")
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

TEST_CASE("Kobber oscillator ranges follow footage labels", "[kobber][processor][oscillator]")
{
	juce::ScopedJuceInitialiser_GUI juceInitializer;
	const std::array ranges { vekt::kobber::parameters::osc1Range, vekt::kobber::parameters::osc2Range, vekt::kobber::parameters::osc3Range };
	const std::array levels { vekt::kobber::parameters::osc1Level, vekt::kobber::parameters::osc2Level, vekt::kobber::parameters::osc3Level };
	const std::array morphs { vekt::kobber::parameters::osc1Morph, vekt::kobber::parameters::osc2Morph, vekt::kobber::parameters::osc3Morph };
	for (std::size_t oscillator = 0; oscillator < ranges.size(); ++oscillator)
		for (int range = 0; range < 5; ++range)
		{
			vekt::kobber::PluginProcessor processor;
			initializeDryVoice(processor);
			setParameter(processor, vekt::kobber::parameters::osc1Level, 0.0f);
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

TEST_CASE("Kobber input Q compensation is inert at zero resonance and changes driven sound", "[kobber][processor][filter][qcomp][slow]")
{
	juce::ScopedJuceInitialiser_GUI juceInitializer;
	for (const auto sampleRate : { 44'100.0, 48'000.0, 96'000.0 })
		for (const auto quality : { 0.0f, 1.0f })
			for (const auto emphasis : { 0.0f, 50.0f, 100.0f })
				for (const auto drive : { 0.0f, 24.0f })
				{
					vekt::kobber::PluginProcessor dry, compensated;
					for (auto* processor : { &dry, &compensated })
					{
						initializeDryVoice(*processor);
						setParameter(*processor, vekt::kobber::parameters::trackingOversampling, quality);
						setParameter(*processor, vekt::kobber::parameters::osc1Level, 100.0f);
						setParameter(*processor, vekt::kobber::parameters::filterCutoff, 1'000.0f);
						setParameter(*processor, vekt::kobber::parameters::filterResonance, emphasis);
						setParameter(*processor, vekt::kobber::parameters::filterDrive, drive);
					}
					setParameter(compensated, vekt::kobber::parameters::filterQCompensation, 1.0f);
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

TEST_CASE("Kobber input Q compensation switches smoothly on a held voice", "[kobber][processor][filter][qcomp]")
{
	juce::ScopedJuceInitialiser_GUI juceInitializer;
	vekt::kobber::PluginProcessor dry, switched;
	for (auto* processor : { &dry, &switched })
	{
		initializeDryVoice(*processor);
		setParameter(*processor, vekt::kobber::parameters::filterCutoff, 1'000.0f);
		setParameter(*processor, vekt::kobber::parameters::filterResonance, 50.0f);
		setParameter(*processor, vekt::kobber::parameters::filterDrive, 24.0f);
		processor->prepareToPlay(48'000.0, 2400);
	}
	juce::AudioBuffer<float> dryBuffer(2, 2400), switchedBuffer(2, 2400);
	juce::MidiBuffer midi;
	midi.addEvent(juce::MidiMessage::noteOn(1, 48, 1.0f), 0);
	renderBlock(dry, dryBuffer, midi);
	renderBlock(switched, switchedBuffer, midi);
	for (const auto enabled : { true, false })
	{
		setParameter(switched, vekt::kobber::parameters::filterQCompensation, enabled ? 1.0f : 0.0f);
		renderBlock(dry, dryBuffer);
		renderBlock(switched, switchedBuffer);
		REQUIRE(switched.coupledWorkSnapshot().unconverged == 0);
		REQUIRE(switched.coupledWorkSnapshot().nonFinite == 0);
		for (int sample = 0; sample < 2400; ++sample)
			REQUIRE(std::isfinite(switchedBuffer.getSample(0, sample)));
		REQUIRE(std::abs(switchedBuffer.getSample(0, 0) - dryBuffer.getSample(0, 0)) < 0.5f);
	}
}

TEST_CASE("Kobber Q compensation defaults off and recalls sound state", "[kobber][processor][state][qcomp]")
{
	juce::ScopedJuceInitialiser_GUI juceInitializer;
	vekt::kobber::PluginProcessor processor;
	auto* parameter = processor.getParameters().getParameter(vekt::kobber::parameters::filterQCompensation);
	REQUIRE(parameter != nullptr);
	REQUIRE(parameter->getDefaultValue() == Catch::Approx(0.0f));
	REQUIRE(parameter->getValue() == Catch::Approx(0.0f));
	for (const auto savedValue : { 1.0f, 0.0f })
	{
		setParameter(processor, vekt::kobber::parameters::filterQCompensation, savedValue);
		juce::MemoryBlock state;
		processor.getStateInformation(state);
		parameter->setValueNotifyingHost(savedValue > 0.5f ? 0.72f : 0.28f);
		processor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
		REQUIRE(parameter->getValue() == Catch::Approx(savedValue));
	}
	setParameter(processor, vekt::kobber::parameters::filterQCompensation, 1.0f);
	for (int index = 0; index < processor.getNumPrograms(); ++index)
	{
		processor.setCurrentProgram(index);
		REQUIRE(parameter->getValue() == Catch::Approx(0.0f));
		setParameter(processor, vekt::kobber::parameters::filterQCompensation, 1.0f);
	}
}

TEST_CASE("Kobber filter Mode defaults to LP, names its landmarks and recalls with state", "[kobber][processor][state][ladder-mode]")
{
	juce::ScopedJuceInitialiser_GUI juceInitializer;
	vekt::kobber::PluginProcessor processor;
	auto* parameter = processor.getParameters().getParameter(vekt::kobber::parameters::filterMode);
	REQUIRE(parameter != nullptr);
	REQUIRE(parameter->getDefaultValue() == Catch::Approx(0.0f));
	for (const auto [value, text] : { std::pair { -1.0f, "LP" }, std::pair { 0.0f, "Notch" }, std::pair { 1.0f, "HP" }, std::pair { 0.5f, "0.50" } })
	{
		REQUIRE(parameter->getText(parameter->convertTo0to1(value), 16) == text);
		REQUIRE(parameter->convertFrom0to1(parameter->getValueForText(text)) == Catch::Approx(value).margin(1.0e-6));
	}
	setParameter(processor, vekt::kobber::parameters::filterMode, 0.25f);
	juce::MemoryBlock state;
	processor.getStateInformation(state);
	setParameter(processor, vekt::kobber::parameters::filterMode, -1.0f);
	processor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
	REQUIRE(processor.getParameters().getRawParameterValue(vekt::kobber::parameters::filterMode)->load() == Catch::Approx(0.25f).margin(1.0e-3));
}

TEST_CASE("Kobber filter Mode sweeps smoothly and changes the held sound", "[kobber][processor][filter][ladder-mode]")
{
	juce::ScopedJuceInitialiser_GUI juceInitializer;
	vekt::kobber::PluginProcessor lowPass, swept;
	for (auto* processor : { &lowPass, &swept })
	{
		initializeDryVoice(*processor);
		setParameter(*processor, vekt::kobber::parameters::filterCutoff, 800.0f);
		setParameter(*processor, vekt::kobber::parameters::filterResonance, 60.0f);
		processor->prepareToPlay(48'000.0, 2400);
	}
	juce::AudioBuffer<float> lowPassBuffer(2, 2400), sweptBuffer(2, 2400);
	juce::MidiBuffer midi;
	midi.addEvent(juce::MidiMessage::noteOn(1, 48, 1.0f), 0);
	renderBlock(lowPass, lowPassBuffer, midi);
	renderBlock(swept, sweptBuffer, midi);
	for (const auto mode : { 0.0f, 1.0f })
	{
		setParameter(swept, vekt::kobber::parameters::filterMode, mode);
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

TEST_CASE("Kobber contour engine measures analog timing and retrigger continuity", "[kobber][processor][contour]")
{
	vekt::kobber::ContourEnvelope envelope;
	envelope.setSampleRate(48'000.0);
	envelope.setParameters({ 0.1f, 0.2f, 0.5f, 0.8f });
	envelope.noteOn();
	for (int i = 0; i < 4'800; ++i) static_cast<void>(envelope.getNextSample());
	REQUIRE(envelope.isActive());
	// At 100 ms the analog attack is at its defined 99% endpoint.
	envelope.reset(); envelope.noteOn();
	for (int i = 0; i < 4'799; ++i) static_cast<void>(envelope.getNextSample());
	REQUIRE(envelope.getNextSample() == Catch::Approx(1.0f).margin(0.002f));
	for (int i = 0; i < 9'600; ++i) static_cast<void>(envelope.getNextSample());
	REQUIRE(envelope.getNextSample() == Catch::Approx(0.5f).margin(0.005f));
	envelope.noteOff();
	for (int i = 0; i < 76'802; ++i) static_cast<void>(envelope.getNextSample());
	REQUIRE_FALSE(envelope.isActive());
	envelope.noteOn();
	for (int i = 0; i < 16'000; ++i) static_cast<void>(envelope.getNextSample());
	envelope.noteOff();
	for (int i = 0; i < 2'400; ++i) static_cast<void>(envelope.getNextSample());
	const auto before = envelope.getNextSample();
	envelope.noteOn();
	const auto after = envelope.getNextSample();
	REQUIRE(after >= before);
	REQUIRE(after - before < 0.001f);
}

TEST_CASE("Kobber amp and filter contours use their independent release times", "[kobber][processor][contour]")
{
	vekt::kobber::ContourEnvelope amp, filter;
	for (auto* envelope : { &amp, &filter }) envelope->setSampleRate(1'000.0);
	amp.setParameters({ 0.01f, 0.3f, 1.0f, 0.1f });
	filter.setParameters({ 0.01f, 0.1f, 1.0f, 0.3f });
	amp.noteOn(); filter.noteOn();
	for (int i = 0; i < 20; ++i) { static_cast<void>(amp.getNextSample()); static_cast<void>(filter.getNextSample()); }
	amp.noteOff(); filter.noteOff();
	for (int i = 0; i < 210; ++i) { static_cast<void>(amp.getNextSample()); static_cast<void>(filter.getNextSample()); }
	REQUIRE_FALSE(amp.isActive());
	REQUIRE(filter.isActive());
	for (int i = 0; i < 410; ++i) static_cast<void>(filter.getNextSample());
	REQUIRE_FALSE(filter.isActive());
}

TEST_CASE("Kobber contour updates held sustain and release without resetting the level", "[kobber][processor][contour]")
{
	vekt::kobber::ContourEnvelope envelope;
	envelope.setSampleRate(1'000.0);
	envelope.setParameters({ 0.01f, 0.05f, 0.5f, 1.0f });
	envelope.noteOn();
	for (int i = 0; i < 200; ++i) static_cast<void>(envelope.getNextSample());
	REQUIRE(envelope.getNextSample() == Catch::Approx(0.5f));
	envelope.setParameters({ 0.01f, 0.05f, 0.8f, 1.0f });
	REQUIRE(envelope.getNextSample() == Catch::Approx(0.8f));
	envelope.noteOff();
	for (int i = 0; i < 100; ++i) static_cast<void>(envelope.getNextSample());
	const auto before = envelope.getNextSample();
	envelope.setParameters({ 0.01f, 0.05f, 0.8f, 0.012f });
	REQUIRE(envelope.getNextSample() < before);
	for (int i = 0; i < 30; ++i) static_cast<void>(envelope.getNextSample());
	REQUIRE_FALSE(envelope.isActive());
}

TEST_CASE("Kobber decay and release tails do not snap at an audible level", "[kobber][processor][contour]")
{
	vekt::kobber::ContourEnvelope envelope;
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

TEST_CASE("Kobber low-note priority ignores higher keys and returns to the lowest held key", "[kobber][processor][midi][contour]")
{
	juce::ScopedJuceInitialiser_GUI juceInitializer;
	vekt::kobber::PluginProcessor processor;
	initializeDryVoice(processor);
	setParameter(processor, vekt::kobber::parameters::performanceMode, 1.0f);
	setParameter(processor, vekt::kobber::parameters::notePriority, 1.0f);
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

TEST_CASE("Kobber glide follows held keys independently of envelope retrigger", "[kobber][processor][midi][glide]")
{
	juce::ScopedJuceInitialiser_GUI juceInitializer;
	for (const auto mode : { 1.0f, 2.0f })
		for (const auto glide : { 0.0f, 1.0f, 2.0f })
			for (const auto gap : { 0, 1, 2 })
			{
				const auto overlap = gap == 0;
				vekt::kobber::PluginProcessor processor;
				initializeDryVoice(processor);
				setParameter(processor, vekt::kobber::parameters::performanceMode, mode);
				setParameter(processor, vekt::kobber::parameters::glideMode, glide);
				setParameter(processor, vekt::kobber::parameters::glideTime, 0.5f);
				setParameter(processor, vekt::kobber::parameters::ampRelease, gap == 2 ? 0.005f : 2.0f);
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

TEST_CASE("Kobber held-key return retains the original velocity", "[kobber][processor][midi]")
{
	juce::ScopedJuceInitialiser_GUI juceInitializer;
	for (const auto mode : { 1.0f, 2.0f })
	{
		vekt::kobber::PluginProcessor processor;
		initializeDryVoice(processor);
		setParameter(processor, vekt::kobber::parameters::performanceMode, mode);
		setParameter(processor, vekt::kobber::parameters::ampVelocity, 100.0f);
		setParameter(processor, vekt::kobber::parameters::filterVelocity, 80.0f);
		setParameter(processor, vekt::kobber::parameters::filterCutoff, 1'000.0f);
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

TEST_CASE("Kobber Legato starts a new gate during a release tail", "[kobber][processor][midi]")
{
	juce::ScopedJuceInitialiser_GUI juceInitializer;
	vekt::kobber::PluginProcessor processor;
	initializeDryVoice(processor);
	setParameter(processor, vekt::kobber::parameters::performanceMode, 2.0f);
	setParameter(processor, vekt::kobber::parameters::ampRelease, 1.0f);
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

TEST_CASE("Kobber pitch bend is independent of glide time", "[kobber][processor][midi][glide]")
{
	juce::ScopedJuceInitialiser_GUI juceInitializer;
	vekt::kobber::PluginProcessor immediate, longGlide;
	for (auto* processor : { &immediate, &longGlide })
	{
		initializeDryVoice(*processor);
		setParameter(*processor, vekt::kobber::parameters::glideMode, 1.0f);
		setParameter(*processor, vekt::kobber::parameters::pitchBendRange, 12.0f);
		processor->prepareToPlay(48'000.0, 4800);
	}
	setParameter(longGlide, vekt::kobber::parameters::glideTime, 5.0f);
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

TEST_CASE("Kobber renders finite stereo MIDI output", "[kobber][processor]")
{
	vekt::kobber::PluginProcessor processor;
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

TEST_CASE("Kobber coupled engine renders deterministically at 1x and 8x", "[kobber][processor][ladder-coupled]")
{
	vekt::kobber::PluginProcessor first, rerun, oversampled, oversampledRerun;
	REQUIRE(first.getName() == "Kobber");
	for (auto* processor : { &first, &rerun, &oversampled, &oversampledRerun })
	{
		initializeDryVoice(*processor);
		setParameter(*processor, vekt::kobber::parameters::filterCutoff, 1'000.0f);
		setParameter(*processor, vekt::kobber::parameters::filterResonance, 85.0f);
		setParameter(*processor, vekt::kobber::parameters::filterDrive, 12.0f);
	}
	setParameter(first, vekt::kobber::parameters::trackingOversampling, 0.0f);
	setParameter(rerun, vekt::kobber::parameters::trackingOversampling, 0.0f);
	setParameter(oversampled, vekt::kobber::parameters::trackingOversampling, 5.0f); // 8x FIR
	setParameter(oversampledRerun, vekt::kobber::parameters::trackingOversampling, 5.0f);
	for (auto* processor : { &first, &rerun, &oversampled, &oversampledRerun })
		processor->prepareToPlay(48'000.0, 128);
	REQUIRE(first.getLatencySamples() == 0);
	REQUIRE(oversampled.getActiveQuality() == vekt::dsp::trackingQualityFrom(5.0f));
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

TEST_CASE("Kobber coupled ladder stays silent and finite across driven notes", "[kobber][processor][ladder-coupled]")
{
	for (const auto rate : { 44'100.0, 48'000.0, 96'000.0 })
	{
		vekt::kobber::PluginProcessor processor;
		initializeDryVoice(processor);
		setParameter(processor, vekt::kobber::parameters::filterResonance, 100.0f);
		setParameter(processor, vekt::kobber::parameters::filterDrive, 24.0f);
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

TEST_CASE("Kobber coupled reprepare clears active audio at every retained quality", "[kobber][processor][ladder-coupled][quality][reset]")
{
	for (const auto rate : { 44'100.0, 48'000.0, 96'000.0 })
	for (const auto quality : kobberQualitySweep)
	{
		CAPTURE(rate, quality);
		vekt::kobber::PluginProcessor restarted, fresh;
		for (auto* processor : { &restarted, &fresh })
		{
			initializeDryVoice(*processor);
			setParameter(*processor, vekt::kobber::parameters::filterCutoff, 1'000.0f);
			setParameter(*processor, vekt::kobber::parameters::filterResonance, 85.0f);
			setParameter(*processor, vekt::kobber::parameters::filterDrive, 12.0f);
			setParameter(*processor, vekt::kobber::parameters::trackingOversampling, static_cast<float>(quality));
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
		REQUIRE(restarted.getActiveQuality() == vekt::dsp::trackingQualityFrom(quality));
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

TEST_CASE("Kobber coupled work counters track an ordinary driven processor", "[kobber][processor][ladder-coupled]")
{
	vekt::kobber::PluginProcessor processor;
	initializeDryVoice(processor);
	setParameter(processor, vekt::kobber::parameters::filterCutoff, 1'000.0f);
	setParameter(processor, vekt::kobber::parameters::filterResonance, 85.0f);
	setParameter(processor, vekt::kobber::parameters::filterDrive, 12.0f);
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

TEST_CASE("Kobber coupled processor exercises every retained quality", "[kobber][processor][ladder-coupled][quality]")
{
	for (const auto quality : kobberQualitySweep)
	{
		vekt::kobber::PluginProcessor coupled;
		initializeDryVoice(coupled);
		setParameter(coupled, vekt::kobber::parameters::trackingOversampling, static_cast<float>(quality));
		setParameter(coupled, vekt::kobber::parameters::filterCutoff, 1'000.0f);
		setParameter(coupled, vekt::kobber::parameters::filterResonance, 85.0f);
		setParameter(coupled, vekt::kobber::parameters::filterDrive, 12.0f);
		coupled.prepareToPlay(48'000.0, 128);
		INFO("quality=" << quality);
		REQUIRE(coupled.getActiveQuality() == vekt::dsp::trackingQualityFrom(quality));
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

TEST_CASE("Kobber coupled reports oversampling latency across retained rates and blocks", "[kobber][processor][ladder-coupled][quality][latency]")
{
	for (const auto rate : { 44'100.0, 48'000.0, 88'200.0, 96'000.0, 192'000.0 })
	for (const auto blockSize : { 128, 257 })
	{
		vekt::dsp::OversamplingBank<float> expected(2);
		expected.prepare(static_cast<std::size_t>(blockSize));
		for (const auto quality : kobberQualitySweep)
		{
			CAPTURE(rate, blockSize, quality);
			vekt::kobber::PluginProcessor coupled;
			initializeDryVoice(coupled);
			setParameter(coupled, vekt::kobber::parameters::trackingOversampling, static_cast<float>(quality));
			coupled.prepareToPlay(rate, blockSize);
			expected.activate(vekt::dsp::trackingQualityFrom(quality));
			const auto latency = expected.getActiveLatencySamples();
			REQUIRE(coupled.getActiveQuality() == vekt::dsp::trackingQualityFrom(quality));
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

TEST_CASE("Kobber coupled quality state recalls into the normal processor", "[kobber][processor][ladder-coupled][quality][state]")
{
	for (const auto quality : kobberQualitySweep)
	{
		vekt::kobber::PluginProcessor source, restored;
		initializeDryVoice(source);
		setParameter(source, vekt::kobber::parameters::trackingOversampling, static_cast<float>(quality));
		setParameter(source, vekt::kobber::parameters::filterCutoff, 1'000.0f);
		setParameter(source, vekt::kobber::parameters::filterResonance, 85.0f);
		setParameter(source, vekt::kobber::parameters::filterDrive, 12.0f);
		juce::MemoryBlock state;
		source.getStateInformation(state);
		REQUIRE(state.getSize() > 0);
		restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
		for (auto* processor : { &source, &restored })
			processor->prepareToPlay(48'000.0, 128);
		INFO("quality=" << quality);
		REQUIRE(source.getActiveQuality() == vekt::dsp::trackingQualityFrom(quality));
		REQUIRE(restored.getActiveQuality() == vekt::dsp::trackingQualityFrom(quality));
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

TEST_CASE("Kobber preset loads clear old audio while allowing new notes in the first callback", "[kobber][processor][ladder-coupled][preset]")
{
	for (const auto quality : kobberQualitySweep)
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
		vekt::kobber::PluginProcessor changed, fresh;
		if (route == 4)
		{
			vekt::presets::Preset preset;
			REQUIRE(changed.getPresetSession().library().loadFactoryPreset(24, preset).wasOk());
			preset.identifier = "kobber-isolation-user-test";
			preset.name = "Isolation Test Bass";
			REQUIRE(repository.save(preset).wasOk());
			for (auto* processor : { &changed, &fresh })
				processor->getPresetSession().library().setUserRepository(&repository);
		}
		for (auto* processor : { &changed, &fresh })
		{
			setParameter(*processor, vekt::kobber::parameters::trackingOversampling, static_cast<float>(quality));
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
		const auto load = [route](vekt::kobber::PluginProcessor& processor)
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
			else REQUIRE(processor.getPresetSession().load("kobber-isolation-user-test", vekt::presets::PresetOrigin::user).wasOk());
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
		REQUIRE(changed.getActiveQuality() == vekt::dsp::trackingQualityFrom(quality));
		REQUIRE(changed.getLatencySamples() == fresh.getLatencySamples());
		REQUIRE(changed.getParameters().getRawParameterValue(vekt::kobber::parameters::filterCutoff)->load()
			== Catch::Approx(fresh.getParameters().getRawParameterValue(vekt::kobber::parameters::filterCutoff)->load()));
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

TEST_CASE("Kobber coupled callbacks remain finite across MIDI and control boundaries", "[kobber][processor][ladder-coupled]")
{
	for (const auto rate : { 44'100.0, 48'000.0 })
		for (const auto blockSize : { 128, 257 })
		{
			vekt::kobber::PluginProcessor coupled;
			initializeDryVoice(coupled);
			setParameter(coupled, vekt::kobber::parameters::performanceMode, 0.0f);
			setParameter(coupled, vekt::kobber::parameters::filterCutoff, 1'000.0f);
			setParameter(coupled, vekt::kobber::parameters::filterResonance, 85.0f);
			setParameter(coupled, vekt::kobber::parameters::filterDrive, 12.0f);
			coupled.prepareToPlay(rate, blockSize);
			juce::AudioBuffer<float> actual(2, blockSize);
			double energy {};
			for (int block = 0; block < 24; ++block)
			{
				if (block == 4 || block == 12 || block == 18)
					{
						setParameter(coupled, vekt::kobber::parameters::filterCutoff,
							block == 4 ? 20'000.0f : block == 12 ? 20.0f : 1'000.0f);
						setParameter(coupled, vekt::kobber::parameters::filterResonance,
							block == 12 ? 100.0f : 85.0f);
						setParameter(coupled, vekt::kobber::parameters::filterDrive,
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

TEST_CASE("Kobber coupled quality changes cut sustained notes immediately", "[kobber][processor][ladder-coupled][quality]")
{
	for (const auto initial : kobberQualitySweep)
	for (const auto target : kobberQualitySweep)
	{
		if (juce::exactlyEqual(initial, target)) continue;
		CAPTURE(initial, target);
		vekt::kobber::PluginProcessor coupled, fresh;
		for (auto* processor : { &coupled, &fresh })
		{
			initializeDryVoice(*processor);
			setParameter(*processor, vekt::kobber::parameters::ampRelease, 0.005f);
			setParameter(*processor, vekt::kobber::parameters::filterCutoff, 1'000.0f);
			setParameter(*processor, vekt::kobber::parameters::filterResonance, 85.0f);
			setParameter(*processor, vekt::kobber::parameters::filterDrive, 12.0f);
			setParameter(*processor, vekt::kobber::parameters::trackingOversampling,
				static_cast<float>(processor == &fresh ? target : initial));
			processor->prepareToPlay(48'000.0, 128);
		}
		juce::AudioBuffer<float> actual(2, 128);
		juce::MidiBuffer held;
		held.addEvent(juce::MidiMessage::controllerEvent(1, 64, 127), 0);
		held.addEvent(juce::MidiMessage::noteOn(1, 48, 0.8f), 0);
		held.addEvent(juce::MidiMessage::noteOff(1, 48), 64);
		renderBlock(coupled, actual, held);
		setParameter(coupled, vekt::kobber::parameters::trackingOversampling, static_cast<float>(target));
		renderBlock(coupled, actual);
		REQUIRE(coupled.getActiveQuality() == vekt::dsp::trackingQualityFrom(target));
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

TEST_CASE("Kobber coupled idle quality changes cover every ordered pair", "[kobber][processor][ladder-coupled][quality]")
{
	for (const auto initial : kobberQualitySweep)
	for (const auto target : kobberQualitySweep)
	{
		if (juce::exactlyEqual(initial, target)) continue;
		CAPTURE(initial, target);
		vekt::kobber::PluginProcessor changed, fresh;
		for (auto* processor : { &changed, &fresh })
		{
			initializeDryVoice(*processor);
			setParameter(*processor, vekt::kobber::parameters::filterCutoff, 1'000.0f);
			setParameter(*processor, vekt::kobber::parameters::filterResonance, 85.0f);
			setParameter(*processor, vekt::kobber::parameters::filterDrive, 12.0f);
			setParameter(*processor, vekt::kobber::parameters::trackingOversampling,
				static_cast<float>(processor == &fresh ? target : initial));
			processor->prepareToPlay(48'000.0, 128);
		}
		juce::AudioBuffer<float> actual(2, 128), expected(2, 128);
		setParameter(changed, vekt::kobber::parameters::trackingOversampling, static_cast<float>(target));
		renderBlock(changed, actual);
		REQUIRE(changed.getActiveQuality() == vekt::dsp::trackingQualityFrom(target));
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

TEST_CASE("Kobber publishes post-output-gain stereo peaks", "[kobber][processor][meter]")
{
	vekt::kobber::PluginProcessor processor;
	setParameter(processor, vekt::kobber::parameters::masterOutput, -6.0f);
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

TEST_CASE("Kobber Ladder cutoff responds smoothly while a note is held", "[kobber][processor][filter]")
{
	vekt::kobber::PluginProcessor processor;
	setParameter(processor, vekt::kobber::parameters::osc1Morph, 2.0f);
	setParameter(processor, vekt::kobber::parameters::osc2Level, 0.0f);
	setParameter(processor, vekt::kobber::parameters::osc3Level, 0.0f);
	setParameter(processor, vekt::kobber::parameters::filterEnvelopeAmount, 0.0f);
	setParameter(processor, vekt::kobber::parameters::filterVelocity, 0.0f);
	setParameter(processor, vekt::kobber::parameters::filterKeyTracking, 0.0f);
	setParameter(processor, vekt::kobber::parameters::filterResonance, 0.0f);
	setParameter(processor, vekt::kobber::parameters::filterCutoff, 120.0f);
	processor.prepareToPlay(48'000.0, 4096);
	juce::AudioBuffer<float> buffer(2, 4096);
	juce::MidiBuffer noteOn;
	noteOn.addEvent(juce::MidiMessage::noteOn(1, 48, 1.0f), 0);
	renderBlock(processor, buffer, noteOn);
	renderBlock(processor, buffer);
	const auto closedBrightness = differenceRms(buffer);

	setParameter(processor, vekt::kobber::parameters::filterCutoff, 12'000.0f);
	renderBlock(processor, buffer);
	for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
		REQUIRE(std::isfinite(buffer.getSample(0, sample)));
	const auto openBrightness = differenceRms(buffer);
	REQUIRE(openBrightness > closedBrightness * 3.0f);
}

TEST_CASE("Kobber Ladder emphasis builds a resonant peak and remains stable", "[kobber][processor][filter]")
{
	auto render = [](float emphasis)
	{
		vekt::kobber::PluginProcessor processor;
		setParameter(processor, vekt::kobber::parameters::osc1Level, 0.0f);
		setParameter(processor, vekt::kobber::parameters::osc2Level, 0.0f);
		setParameter(processor, vekt::kobber::parameters::osc3Level, 0.0f);
		setParameter(processor, vekt::kobber::parameters::noiseType, 1.0f);
		setParameter(processor, vekt::kobber::parameters::noiseLevel, 50.0f);
		setParameter(processor, vekt::kobber::parameters::filterCutoff, 1'000.0f);
		setParameter(processor, vekt::kobber::parameters::filterEnvelopeAmount, 0.0f);
		setParameter(processor, vekt::kobber::parameters::filterVelocity, 0.0f);
		setParameter(processor, vekt::kobber::parameters::filterKeyTracking, 0.0f);
		setParameter(processor, vekt::kobber::parameters::filterResonance, emphasis);
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

TEST_CASE("Kobber uncompensated Ladder loses passband level with emphasis", "[kobber][processor][filter][qcomp][slow]")
{
	juce::ScopedJuceInitialiser_GUI juceInitializer;
	auto levelFor = [](float oscillatorLevel, float cutoff, float emphasis)
	{
		vekt::kobber::PluginProcessor processor;
		initializeDryVoice(processor);
		setParameter(processor, vekt::kobber::parameters::osc1Level, oscillatorLevel);
		setParameter(processor, vekt::kobber::parameters::osc1Morph, 0.0f);
		setParameter(processor, vekt::kobber::parameters::osc2Level, 0.0f);
		setParameter(processor, vekt::kobber::parameters::osc3Level, 0.0f);
		setParameter(processor, vekt::kobber::parameters::filterCutoff, cutoff);
		setParameter(processor, vekt::kobber::parameters::filterEnvelopeAmount, 0.0f);
		setParameter(processor, vekt::kobber::parameters::filterVelocity, 0.0f);
		setParameter(processor, vekt::kobber::parameters::filterKeyTracking, 0.0f);
		setParameter(processor, vekt::kobber::parameters::filterDrive, 0.0f);
		setParameter(processor, vekt::kobber::parameters::filterResonance, emphasis);
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

TEST_CASE("Kobber Ladder self-oscillates at maximum emphasis", "[kobber][processor][filter][slow]")
{
	for (const auto quality : { 0.0f, 1.0f })
		for (const auto sampleRate : { 44'100.0f, 48'000.0f, 96'000.0f })
			for (const auto cutoff : { 250.0f, 1'000.0f, 4'000.0f })
		{
			vekt::kobber::PluginProcessor processor;
			setParameter(processor, vekt::kobber::parameters::trackingOversampling, quality);
			setParameter(processor, vekt::kobber::parameters::osc1Level, 0.0f);
			setParameter(processor, vekt::kobber::parameters::osc2Level, 0.0f);
			setParameter(processor, vekt::kobber::parameters::osc3Level, 0.0f);
			setParameter(processor, vekt::kobber::parameters::noiseType, 1.0f);
			setParameter(processor, vekt::kobber::parameters::noiseLevel, 5.0f);
			setParameter(processor, vekt::kobber::parameters::filterCutoff, cutoff);
			setParameter(processor, vekt::kobber::parameters::filterEnvelopeAmount, 0.0f);
			setParameter(processor, vekt::kobber::parameters::filterVelocity, 0.0f);
			setParameter(processor, vekt::kobber::parameters::filterKeyTracking, 0.0f);
			setParameter(processor, vekt::kobber::parameters::filterDrive, 0.0f);
			setParameter(processor, vekt::kobber::parameters::filterResonance, 100.0f);
			setParameter(processor, vekt::kobber::parameters::ampSustain, 100.0f);
			setParameter(processor, vekt::kobber::parameters::ampVelocity, 0.0f);
			setParameter(processor, vekt::kobber::parameters::unison, 0.0f);
			setParameter(processor, vekt::kobber::parameters::voiceWidth, 0.0f);
			setParameter(processor, vekt::kobber::parameters::masterOutput, 0.0f);
			processor.prepareToPlay(sampleRate, 4096);
			juce::AudioBuffer<float> buffer(2, 4096);
			juce::MidiBuffer noteOn;
			noteOn.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
			renderBlock(processor, buffer, noteOn);
			// A delay-free nonlinear ladder does not spontaneously leave an exact
			// zero state. Excite it once, then observe the unforced ringdown.
			setParameter(processor, vekt::kobber::parameters::noiseLevel, 0.0f);
			// A steady single tone, not merely a dominant one: two oscillations of similar size would beat, so the
			// per-block (~43-93 ms) RMS and peak over the second half of the observation must not move.
			float minimumBlockRms { std::numeric_limits<float>::max() }, maximumBlockRms {};
			float minimumBlockPeak { std::numeric_limits<float>::max() }, maximumBlockPeak {};
			for (int block = 0; block < 24; ++block)
			{
				renderBlock(processor, buffer);
				if (block < 12) continue;
				const auto blockRms = rms(buffer);
				const auto blockPeak = buffer.getMagnitude(0, 0, buffer.getNumSamples());
				minimumBlockRms = std::min(minimumBlockRms, blockRms);
				maximumBlockRms = std::max(maximumBlockRms, blockRms);
				minimumBlockPeak = std::min(minimumBlockPeak, blockPeak);
				maximumBlockPeak = std::max(maximumBlockPeak, blockPeak);
			}
			const auto settledRms = rms(buffer);
			const auto selfOscPowerRms = stereoPowerRms(buffer);
			const auto [frequency, magnitude] = dominantFrequency(buffer, cutoff * 0.9f, cutoff * 1.1f, sampleRate);
			const auto secondHarmonic = sinusoidMagnitude(buffer, frequency * 2.0f, sampleRate);
			const auto thirdHarmonic = sinusoidMagnitude(buffer, frequency * 3.0f, sampleRate);
			INFO("quality=" << quality << ", sample rate=" << sampleRate << ", cutoff=" << cutoff << ", fundamental=" << frequency
				<< " Hz / " << magnitude << ", second=" << secondHarmonic << ", third=" << thirdHarmonic
				<< ", left RMS=" << settledRms << ", stereo power RMS=" << selfOscPowerRms
				<< ", block RMS " << minimumBlockRms << ".." << maximumBlockRms << ", block peak " << minimumBlockPeak << ".." << maximumBlockPeak);
			REQUIRE(maximumBlockRms < minimumBlockRms * 1.03f);   // 0.26 dB
			REQUIRE(maximumBlockPeak < minimumBlockPeak * 1.03f);
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

TEST_CASE("Kobber Ladder self-oscillation is audible through a preset-style voice path", "[kobber][processor][filter]")
{
	vekt::kobber::PluginProcessor processor;
	setParameter(processor, vekt::kobber::parameters::osc1Level, 0.0f);
	setParameter(processor, vekt::kobber::parameters::osc2Level, 0.0f);
	setParameter(processor, vekt::kobber::parameters::osc3Level, 0.0f);
	setParameter(processor, vekt::kobber::parameters::noiseType, 1.0f);
	setParameter(processor, vekt::kobber::parameters::noiseLevel, 5.0f);
	setParameter(processor, vekt::kobber::parameters::filterCutoff, 1'000.0f);
	setParameter(processor, vekt::kobber::parameters::filterEnvelopeAmount, 0.0f);
	setParameter(processor, vekt::kobber::parameters::filterVelocity, 0.0f);
	setParameter(processor, vekt::kobber::parameters::filterKeyTracking, 0.0f);
	setParameter(processor, vekt::kobber::parameters::filterDrive, 0.0f);
	setParameter(processor, vekt::kobber::parameters::filterResonance, 100.0f);
	setParameter(processor, vekt::kobber::parameters::ampSustain, 64.0f);
	setParameter(processor, vekt::kobber::parameters::ampVelocity, 55.0f);
	setParameter(processor, vekt::kobber::parameters::unison, 1.0f);
	setParameter(processor, vekt::kobber::parameters::unisonSpread, 48.0f);
	setParameter(processor, vekt::kobber::parameters::voiceWidth, 18.0f);
	setParameter(processor, vekt::kobber::parameters::masterOutput, -7.0f);
	processor.prepareToPlay(48'000.0, 4096);
	juce::AudioBuffer<float> buffer(2, 4096);
	juce::MidiBuffer noteOn;
	noteOn.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
	renderBlock(processor, buffer, noteOn);
	setParameter(processor, vekt::kobber::parameters::noiseLevel, 0.0f);
	for (int block = 0; block < 12; ++block) renderBlock(processor, buffer);
	REQUIRE(stereoPowerRms(buffer) > 0.025f);
}

TEST_CASE("Kobber Ladder enters self-oscillation when emphasis reaches maximum in real time", "[kobber][processor][filter]")
{
	vekt::kobber::PluginProcessor processor;
	setParameter(processor, vekt::kobber::parameters::osc1Level, 0.0f);
	setParameter(processor, vekt::kobber::parameters::osc2Level, 0.0f);
	setParameter(processor, vekt::kobber::parameters::osc3Level, 0.0f);
	setParameter(processor, vekt::kobber::parameters::noiseType, 0.0f);
	setParameter(processor, vekt::kobber::parameters::noiseLevel, 0.0f);
	setParameter(processor, vekt::kobber::parameters::filterCutoff, 1'000.0f);
	setParameter(processor, vekt::kobber::parameters::filterEnvelopeAmount, 0.0f);
	setParameter(processor, vekt::kobber::parameters::filterVelocity, 0.0f);
	setParameter(processor, vekt::kobber::parameters::filterKeyTracking, 0.0f);
	setParameter(processor, vekt::kobber::parameters::filterDrive, 0.0f);
	setParameter(processor, vekt::kobber::parameters::filterResonance, 0.0f);
	setParameter(processor, vekt::kobber::parameters::ampSustain, 100.0f);
	setParameter(processor, vekt::kobber::parameters::ampVelocity, 0.0f);
	setParameter(processor, vekt::kobber::parameters::unison, 0.0f);
	setParameter(processor, vekt::kobber::parameters::voiceWidth, 0.0f);
	setParameter(processor, vekt::kobber::parameters::masterOutput, 0.0f);
	processor.prepareToPlay(48'000.0, 512);
	juce::AudioBuffer<float> buffer(2, 512);
	juce::MidiBuffer noteOn;
	noteOn.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
	renderBlock(processor, buffer, noteOn);
	for (int block = 0; block < 8; ++block) renderBlock(processor, buffer);
	REQUIRE(stereoRms(buffer) < 1.0e-6f);

	setParameter(processor, vekt::kobber::parameters::filterResonance, 100.0f);
	for (int block = 0; block < 120; ++block) renderBlock(processor, buffer);
	// The coupled model preserves the zero equilibrium without excitation.
	REQUIRE(stereoRms(buffer) == 0.0f);
	setParameter(processor, vekt::kobber::parameters::noiseType, 1.0f);
	setParameter(processor, vekt::kobber::parameters::noiseLevel, 5.0f);
	renderBlock(processor, buffer);
	setParameter(processor, vekt::kobber::parameters::noiseLevel, 0.0f);
	for (int block = 0; block < 120; ++block) renderBlock(processor, buffer);
	REQUIRE(stereoPowerRms(buffer) > 0.1f);
	const auto [frequency, magnitude] = dominantFrequency(buffer, 700.0f, 1'400.0f, 48'000.0f);
	REQUIRE(frequency > 750.0f);
	REQUIRE(frequency < 1'300.0f);
	REQUIRE(magnitude > rms(buffer));
}

TEST_CASE("Kobber Ladder develops an audible cutoff tone when GUI-style emphasis is raised over an active oscillator", "[kobber][processor][filter]")
{
	vekt::kobber::PluginProcessor processor;
	setParameter(processor, vekt::kobber::parameters::osc1Level, 45.0f);
	setParameter(processor, vekt::kobber::parameters::osc1Morph, 0.0f);
	setParameter(processor, vekt::kobber::parameters::osc2Level, 0.0f);
	setParameter(processor, vekt::kobber::parameters::osc3Level, 0.0f);
	setParameter(processor, vekt::kobber::parameters::noiseType, 0.0f);
	setParameter(processor, vekt::kobber::parameters::noiseLevel, 0.0f);
	setParameter(processor, vekt::kobber::parameters::filterCutoff, 1'000.0f);
	setParameter(processor, vekt::kobber::parameters::filterEnvelopeAmount, 0.0f);
	setParameter(processor, vekt::kobber::parameters::filterVelocity, 0.0f);
	setParameter(processor, vekt::kobber::parameters::filterKeyTracking, 0.0f);
	setParameter(processor, vekt::kobber::parameters::filterDrive, 0.0f);
	setParameter(processor, vekt::kobber::parameters::filterResonance, 0.0f);
	setParameter(processor, vekt::kobber::parameters::ampSustain, 100.0f);
	setParameter(processor, vekt::kobber::parameters::ampVelocity, 0.0f);
	setParameter(processor, vekt::kobber::parameters::unison, 0.0f);
	setParameter(processor, vekt::kobber::parameters::voiceWidth, 0.0f);
	setParameter(processor, vekt::kobber::parameters::masterOutput, 0.0f);
	processor.prepareToPlay(48'000.0, 512);
	juce::AudioBuffer<float> buffer(2, 512);
	juce::MidiBuffer noteOn;
	noteOn.addEvent(juce::MidiMessage::noteOn(1, 57, 1.0f), 0);
	renderBlock(processor, buffer, noteOn);
	for (int block = 0; block < 8; ++block) renderBlock(processor, buffer);
	const auto lowResonanceCutoffTone = sinusoidMagnitude(buffer, 1'000.0f, 48'000.0f);

	setParameter(processor, vekt::kobber::parameters::filterResonance, 100.0f);
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

TEST_CASE("Kobber Ladder does not self-oscillate below the upper emphasis range", "[kobber][processor][filter]")
{
	vekt::kobber::PluginProcessor processor;
	setParameter(processor, vekt::kobber::parameters::osc1Level, 0.0f);
	setParameter(processor, vekt::kobber::parameters::osc2Level, 0.0f);
	setParameter(processor, vekt::kobber::parameters::osc3Level, 0.0f);
	setParameter(processor, vekt::kobber::parameters::noiseType, 0.0f);
	setParameter(processor, vekt::kobber::parameters::noiseLevel, 0.0f);
	setParameter(processor, vekt::kobber::parameters::filterCutoff, 1'000.0f);
	setParameter(processor, vekt::kobber::parameters::filterEnvelopeAmount, 0.0f);
	setParameter(processor, vekt::kobber::parameters::filterVelocity, 0.0f);
	setParameter(processor, vekt::kobber::parameters::filterKeyTracking, 0.0f);
	setParameter(processor, vekt::kobber::parameters::filterResonance, 70.0f);
	setParameter(processor, vekt::kobber::parameters::ampSustain, 100.0f);
	setParameter(processor, vekt::kobber::parameters::ampVelocity, 0.0f);
	setParameter(processor, vekt::kobber::parameters::unison, 0.0f);
	setParameter(processor, vekt::kobber::parameters::voiceWidth, 0.0f);
	setParameter(processor, vekt::kobber::parameters::masterOutput, 0.0f);
	processor.prepareToPlay(48'000.0, 4096);
	juce::AudioBuffer<float> buffer(2, 4096);
	juce::MidiBuffer noteOn;
	noteOn.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
	renderBlock(processor, buffer, noteOn);
	for (int block = 0; block < 12; ++block) renderBlock(processor, buffer);
	REQUIRE(stereoRms(buffer) < 1.0e-4f);
}

TEST_CASE("Kobber Ladder keyboard tracking follows one octave per keyboard octave", "[kobber][processor][filter]")
{
	auto brightnessFor = [](int note)
	{
		vekt::kobber::PluginProcessor processor;
		setParameter(processor, vekt::kobber::parameters::osc1Level, 0.0f);
		setParameter(processor, vekt::kobber::parameters::noiseType, 1.0f);
		setParameter(processor, vekt::kobber::parameters::noiseLevel, 50.0f);
		setParameter(processor, vekt::kobber::parameters::filterCutoff, 500.0f);
		setParameter(processor, vekt::kobber::parameters::filterEnvelopeAmount, 0.0f);
		setParameter(processor, vekt::kobber::parameters::filterVelocity, 0.0f);
		setParameter(processor, vekt::kobber::parameters::filterKeyTracking, 100.0f);
		setParameter(processor, vekt::kobber::parameters::filterResonance, 0.0f);
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

TEST_CASE("Kobber Ladder contour is unipolar with a wide full-scale sweep", "[kobber][processor][filter]")
{
	auto brightnessFor = [](float contour)
	{
		vekt::kobber::PluginProcessor processor;
		setParameter(processor, vekt::kobber::parameters::osc1Level, 0.0f);
		setParameter(processor, vekt::kobber::parameters::noiseType, 1.0f);
		setParameter(processor, vekt::kobber::parameters::noiseLevel, 50.0f);
		setParameter(processor, vekt::kobber::parameters::filterCutoff, 1'000.0f);
		setParameter(processor, vekt::kobber::parameters::filterEnvelopeAmount, contour);
		setParameter(processor, vekt::kobber::parameters::filterAttack, 0.0005f);
		setParameter(processor, vekt::kobber::parameters::filterSustain, 100.0f);
		setParameter(processor, vekt::kobber::parameters::filterVelocity, 0.0f);
		setParameter(processor, vekt::kobber::parameters::filterKeyTracking, 0.0f);
		processor.prepareToPlay(48'000.0, 4096);
		juce::AudioBuffer<float> buffer(2, 4096);
		juce::MidiBuffer noteOn;
		noteOn.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
		renderBlock(processor, buffer, noteOn);
		renderBlock(processor, buffer);
		return differenceRms(buffer);
	};
	vekt::kobber::PluginProcessor processor;
	auto* parameter = processor.getParameters().getParameter(vekt::kobber::parameters::filterEnvelopeAmount);
	REQUIRE(parameter != nullptr);
	REQUIRE(parameter->convertFrom0to1(0.0f) == 0.0f);
	REQUIRE(parameter->convertFrom0to1(1.0f) == 100.0f);
	REQUIRE(brightnessFor(100.0f) > brightnessFor(0.0f) * 5.0f);
}

TEST_CASE("Kobber Ladder drive adds harmonics without acting as output gain", "[kobber][processor][filter]")
{
	auto render = [](float drive)
	{
		vekt::kobber::PluginProcessor processor;
		setParameter(processor, vekt::kobber::parameters::osc1Morph, 0.0f);
		setParameter(processor, vekt::kobber::parameters::osc2Level, 0.0f);
		setParameter(processor, vekt::kobber::parameters::osc3Level, 0.0f);
		setParameter(processor, vekt::kobber::parameters::filterCutoff, 20'000.0f);
		setParameter(processor, vekt::kobber::parameters::filterEnvelopeAmount, 0.0f);
		setParameter(processor, vekt::kobber::parameters::filterVelocity, 0.0f);
		setParameter(processor, vekt::kobber::parameters::filterKeyTracking, 0.0f);
		setParameter(processor, vekt::kobber::parameters::filterResonance, 0.0f);
		setParameter(processor, vekt::kobber::parameters::filterDrive, drive);
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
	REQUIRE(drivenRms < cleanRms / static_cast<float>(vekt::kobber::widthSineGain) * 4.0f);
}

TEST_CASE("Kobber maximum resonance keeps floating-point peaks and obeys master trim", "[kobber][processor][filter][headroom][slow]")
{
	for (const auto quality : { 0.0f, 1.0f })
		for (const auto drive : { 0.0f, 24.0f })
			for (const auto compensated : { false, true })
			{
				vekt::kobber::PluginProcessor unity, trimmed;
				for (auto* processor : { &unity, &trimmed })
				{
					initializeDryVoice(*processor);
					setParameter(*processor, vekt::kobber::parameters::trackingOversampling, quality);
					setParameter(*processor, vekt::kobber::parameters::osc1Level, 100.0f);
					setParameter(*processor, vekt::kobber::parameters::filterCutoff, 1'000.0f);
					setParameter(*processor, vekt::kobber::parameters::filterResonance, 100.0f);
					setParameter(*processor, vekt::kobber::parameters::filterDrive, drive);
					setParameter(*processor, vekt::kobber::parameters::filterQCompensation, compensated ? 1.0f : 0.0f);
				}
				setParameter(unity, vekt::kobber::parameters::masterOutput, 0.0f);
				setParameter(trimmed, vekt::kobber::parameters::masterOutput, -12.0f);
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
							REQUIRE(trimmedValue == Catch::Approx(value * vekt::kobber::dbToGain(-12.0f)).margin(2.0e-5f));
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

TEST_CASE("Kobber preserves APVTS project state", "[kobber][processor]")
{
	vekt::kobber::PluginProcessor source;
	setParameter(source, vekt::kobber::parameters::filterCutoff, 2'345.0f);
	juce::MemoryBlock state;
	source.getStateInformation(state);
	vekt::kobber::PluginProcessor restored;
	restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
	REQUIRE(restored.getParameters().getRawParameterValue(vekt::kobber::parameters::filterCutoff)->load() == Catch::Approx(2'345.0f));
}

TEST_CASE("Kobber rejects project states that are not exactly its current format without changing live state", "[kobber][processor][state]")
{
	vekt::kobber::PluginProcessor source;
	juce::MemoryBlock current;
	source.getStateInformation(current);
	const auto bytesOf = [](const juce::String& text) { return juce::MemoryBlock(text.toRawUTF8(), text.getNumBytesAsUTF8()); };
	// The saved document with one change.
	const auto changed = [&current, &bytesOf](const std::function<void(juce::DynamicObject&)>& change)
	{
		juce::var project;
		REQUIRE(juce::JSON::parse(juce::String::fromUTF8(static_cast<const char*>(current.getData()),
			static_cast<int>(current.getSize())), project).wasOk());
		change(*project.getDynamicObject());
		return bytesOf(juce::JSON::toString(project));
	};
	const auto set = [](const char* key, juce::var value) { return [=](juce::DynamicObject& project) { project.setProperty(key, value); }; };
	// A parameter written as a number too large for a double, which JSON parsers read as infinity.
	const auto overflowing = [&changed](const char* identifier)
	{
		const auto withMarker = changed([identifier](juce::DynamicObject& project)
			{ project.getProperty("parameters").getDynamicObject()->setProperty(identifier, 123456.5); });
		const auto text = juce::String::fromUTF8(static_cast<const char*>(withMarker.getData()), static_cast<int>(withMarker.getSize()));
		REQUIRE(text.contains("123456.5"));
		const auto replaced = text.replace("123456.5", "1e999");
		return juce::MemoryBlock(replaced.toRawUTF8(), replaced.getNumBytesAsUTF8());
	};
	juce::MemoryBlock valueTree; // what a JUCE ValueTree project looked like
	{
		juce::MemoryOutputStream stream(valueTree, false);
		source.getParameters().copyState().writeToStream(stream);
	}
	const std::vector<std::pair<const char*, juce::MemoryBlock>> rejected {
		{ "schema 0", changed(set("schemaVersion", 0)) },
		{ "schema 2", changed(set("schemaVersion", 2)) },
		{ "other format", changed(set("format", "vekt.preset")) },
		{ "other product", changed(set("product", "com.vekt.rav")) },
		{ "text parameter value", changed([](juce::DynamicObject& project)
			{ project.getProperty("parameters").getDynamicObject()->setProperty(vekt::kobber::parameters::filterCutoff, "high"); }) },
		{ "metadata not an object", changed(set("metadata", 5)) },
		{ "overflowing parameter value", overflowing(vekt::kobber::parameters::filterCutoff) },
		{ "overflowing unknown parameter value", overflowing("unknownParameter") },
		{ "not JSON", bytesOf("{") },
		{ "JUCE ValueTree", valueTree } };
	for (const auto& [name, data] : rejected)
	{
		INFO(name);
		vekt::kobber::PluginProcessor restored;
		setParameter(restored, vekt::kobber::parameters::filterCutoff, 4'321.0f);
		setParameter(restored, vekt::kobber::parameters::heldKeyReturn, 0.0f);
		setParameter(restored, vekt::kobber::parameters::filterQCompensation, 1.0f);
		restored.setStateInformation(data.getData(), static_cast<int>(data.getSize()));
		REQUIRE(restored.getParameters().getRawParameterValue(vekt::kobber::parameters::filterCutoff)->load() == Catch::Approx(4'321.0f));
		REQUIRE(restored.getParameters().getRawParameterValue(vekt::kobber::parameters::heldKeyReturn)->load() == Catch::Approx(0.0f));
		REQUIRE(restored.getParameters().getRawParameterValue(vekt::kobber::parameters::filterQCompensation)->load() == Catch::Approx(1.0f));
	}
	// The unchanged document restores.
	vekt::kobber::PluginProcessor restored;
	setParameter(restored, vekt::kobber::parameters::filterCutoff, 4'321.0f);
	restored.setStateInformation(current.getData(), static_cast<int>(current.getSize()));
	REQUIRE(restored.getParameters().getRawParameterValue(vekt::kobber::parameters::filterCutoff)->load() != Catch::Approx(4'321.0f));
}
TEST_CASE("Kobber voice count changes cut active notes immediately", "[kobber][processor]")
{
	vekt::kobber::PluginProcessor processor;
	processor.prepareToPlay(48'000.0, 512);
	juce::AudioBuffer<float> buffer(2, 128);
	juce::MidiBuffer on;
	on.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
	renderBlock(processor, buffer, on);
	REQUIRE(buffer.getMagnitude(0, buffer.getNumSamples()) > 0.0f);
	setParameter(processor, vekt::kobber::parameters::voiceCount, 4.0f);
	renderBlock(processor, buffer);
	REQUIRE(buffer.getMagnitude(0, buffer.getNumSamples()) == 0.0f);
	renderBlock(processor, buffer, on);
	REQUIRE(buffer.getMagnitude(0, buffer.getNumSamples()) > 0.0f);
}

TEST_CASE("Kobber provides 26 categorized factory presets", "[kobber][processor]")
{
	vekt::kobber::PluginProcessor processor;
	auto& session = processor.getPresetSession();
	const auto& catalog = session.library();
	REQUIRE(catalog.factoryPresetCount() == 26);
	REQUIRE(catalog.folders(vekt::presets::PresetOrigin::factory).size() == 6);
	REQUIRE(processor.getNumPrograms() == 26);
	processor.setCurrentProgram(23);
	REQUIRE(processor.getCurrentProgram() == 23);
	REQUIRE(processor.getProgramName(23) == "Transmission FX");
}

TEST_CASE("Kobber factory presets load with their stored values, including LFO settings", "[kobber][processor][preset]")
{
	vekt::kobber::PluginProcessor processor;
	const auto& catalog = processor.getPresetSession().library();
	for (std::size_t index = 0; index < catalog.factoryPresetCount(); ++index)
	{
		vekt::presets::Preset preset;
		REQUIRE(catalog.loadFactoryPreset(index, preset).wasOk());
		CAPTURE(preset.name);
		processor.setCurrentProgram(static_cast<int>(index));
		REQUIRE(processor.getCurrentProgram() == static_cast<int>(index));
		// Every file carries every sound parameter; each must exist and land unchanged.
		REQUIRE(preset.parameters.size() == vekt::kobber::parameters::soundParameterIds.size());
		for (const auto& parameter : preset.parameters)
		{
			CAPTURE(parameter.identifier);
			const auto* value = processor.getParameters().getRawParameterValue(parameter.identifier);
			REQUIRE(value != nullptr);
			REQUIRE(value->load() == Catch::Approx(parameter.value).margin(1.0e-3));
		}
	}
}

TEST_CASE("Kobber factory presets are not marked modified right after loading", "[kobber][processor][preset]")
{
	// Non-zero continuous LFO depths come back a few 1e-6 off after the 0..1 parameter round trip; that
	// must not read as an edit (it used to show a "*" on every preset with LFO settings).
	vekt::kobber::PluginProcessor processor;
	auto& session = processor.getPresetSession();
	for (std::size_t index = 0; index < session.library().factoryPresetCount(); ++index)
	{
		processor.setCurrentProgram(static_cast<int>(index));
		REQUIRE(session.loaded().has_value());
		CAPTURE(session.loaded()->name);
		REQUIRE_FALSE(session.modified());
	}
	// A real edit still counts.
	auto* depth = processor.getParameters().getParameter(vekt::kobber::parameters::lfos[0].width[0]);
	depth->setValueNotifyingHost(depth->convertTo0to1(12.5f));
	REQUIRE(session.modified());
}

TEST_CASE("Kobber Classic Three Bass uses three oscillators", "[kobber][processor][preset]")
{
	vekt::kobber::PluginProcessor processor;
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
	REQUIRE(value(vekt::kobber::parameters::osc1Range) == Catch::Approx(0.0f));
	REQUIRE(value(vekt::kobber::parameters::osc2Range) == Catch::Approx(1.0f));
	REQUIRE(value(vekt::kobber::parameters::osc3Range) == Catch::Approx(1.0f));
	REQUIRE(value(vekt::kobber::parameters::osc1Level) > 0.0f);
	REQUIRE(value(vekt::kobber::parameters::osc2Level) > 0.0f);
	REQUIRE(value(vekt::kobber::parameters::osc3Level) > 0.0f);
}

TEST_CASE("Kobber factory presets use diverse oscillator and mixer designs", "[kobber][processor][preset]")
{
	vekt::kobber::PluginProcessor processor;
	const auto& catalog = processor.getPresetSession().library();
	std::set<juce::String> oscillatorShapes, oscillatorTunings;
	std::set<float> noiseLevels, voicePans;
	for (std::size_t index = 0; index < catalog.factoryPresetCount(); ++index)
	{
		vekt::presets::Preset preset;
		REQUIRE(catalog.loadFactoryPreset(index, preset).wasOk());
		REQUIRE(preset.soundSchemaVersion == 12);
		const auto value = [&preset](const char* identifier)
		{
			const auto found = std::find_if(preset.parameters.begin(), preset.parameters.end(), [identifier](const auto& parameter)
			{
				return parameter.identifier == identifier;
			});
			REQUIRE(found != preset.parameters.end());
			return found->value;
		};
		oscillatorShapes.insert(juce::String(value(vekt::kobber::parameters::osc1Morph), 3) + "/"
			+ juce::String(value(vekt::kobber::parameters::osc2Morph), 3) + "/"
			+ juce::String(value(vekt::kobber::parameters::osc3Morph), 3) + ":"
			+ juce::String(value(vekt::kobber::parameters::osc1PulseWidth), 2) + "/"
			+ juce::String(value(vekt::kobber::parameters::osc2PulseWidth), 2) + "/"
			+ juce::String(value(vekt::kobber::parameters::osc3PulseWidth), 2));
		oscillatorTunings.insert(juce::String(value(vekt::kobber::parameters::osc1Octave), 0) + "/"
			+ juce::String(value(vekt::kobber::parameters::osc2Octave), 0) + "/"
			+ juce::String(value(vekt::kobber::parameters::osc3Octave), 0) + ":"
			+ juce::String(value(vekt::kobber::parameters::osc1Fine), 1) + "/"
			+ juce::String(value(vekt::kobber::parameters::osc2Fine), 1) + "/"
			+ juce::String(value(vekt::kobber::parameters::osc3Fine), 1));
		noiseLevels.insert(value(vekt::kobber::parameters::noiseLevel));
		voicePans.insert(value(vekt::kobber::parameters::voiceWidth));
		REQUIRE(value(vekt::kobber::parameters::filterQCompensation) == Catch::Approx(0.0f));
	}
	REQUIRE(oscillatorShapes.size() >= 20);
	REQUIRE(oscillatorTunings.size() >= 20);
	REQUIRE(noiseLevels.size() >= 10);
	REQUIRE(voicePans.size() >= 10);
}

TEST_CASE("Kobber rejects obsolete pre-alpha preset schemas without mutation", "[kobber][processor][preset]")
{
	vekt::kobber::PluginProcessor processor;
	setParameter(processor, vekt::kobber::parameters::filterCutoff, 4'321.0f);
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
		REQUIRE(processor.getParameters().getRawParameterValue(vekt::kobber::parameters::filterCutoff)->load() == Catch::Approx(4'321.0f));
	}
	REQUIRE(processor.getPresetSession().prepare(current).wasOk());
}

TEST_CASE("Kobber preset changes stop voices from the previous patch", "[kobber][processor][preset]")
{
	vekt::kobber::PluginProcessor processor;
	setParameter(processor, vekt::kobber::parameters::ampRelease, 20.0f);
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

TEST_CASE("Kobber held-key return is configurable", "[kobber][processor][midi]")
{
	for (const auto mode : { 1.0f, 2.0f })
		for (const auto heldKeyReturn : { 0.0f, 1.0f })
	{
		vekt::kobber::PluginProcessor processor;
		setParameter(processor, vekt::kobber::parameters::performanceMode, mode);
		setParameter(processor, vekt::kobber::parameters::heldKeyReturn, heldKeyReturn);
		setParameter(processor, vekt::kobber::parameters::ampRelease, 0.005f);
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

TEST_CASE("Kobber active-note transitions preserve the sample boundary", "[kobber][processor][midi][declick]")
{
	for (const auto mode : { 1.0f, 2.0f })
	{
		vekt::kobber::PluginProcessor processor;
		setParameter(processor, vekt::kobber::parameters::performanceMode, mode);
		setParameter(processor, vekt::kobber::parameters::osc1Morph, 2.0f);
		setParameter(processor, vekt::kobber::parameters::osc2Level, 0.0f);
		setParameter(processor, vekt::kobber::parameters::osc3Level, 0.0f);
		setParameter(processor, vekt::kobber::parameters::filterCutoff, 12'000.0f);
		setParameter(processor, vekt::kobber::parameters::filterEnvelopeAmount, 0.0f);
		setParameter(processor, vekt::kobber::parameters::ampAttack, 0.0005f);
		// Sample-exact boundary check: at 1x no resampling filter smears the voice's own crossfade.
		setParameter(processor, vekt::kobber::parameters::trackingOversampling, 0.0f);
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

TEST_CASE("Kobber active-note release preserves the sample boundary", "[kobber][processor][midi][declick]")
{
	vekt::kobber::PluginProcessor processor;
	setParameter(processor, vekt::kobber::parameters::performanceMode, 2.0f);
	setParameter(processor, vekt::kobber::parameters::osc1Morph, 3.0f);
	setParameter(processor, vekt::kobber::parameters::osc2Level, 0.0f);
	setParameter(processor, vekt::kobber::parameters::osc3Level, 0.0f);
	setParameter(processor, vekt::kobber::parameters::filterCutoff, 12'000.0f);
	setParameter(processor, vekt::kobber::parameters::filterEnvelopeAmount, 0.0f);
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

TEST_CASE("Kobber poly repeated keys retrigger their own voice instead of stacking", "[kobber][processor][midi]")
{
	const auto prepared = []
	{
		auto processor = std::make_unique<vekt::kobber::PluginProcessor>();
		setParameter(*processor, vekt::kobber::parameters::performanceMode, 0.0f);
		setParameter(*processor, vekt::kobber::parameters::ampRelease, 5.0f);
		processor->prepareToPlay(48'000.0, 256);
		return processor;
	};
	juce::AudioBuffer<float> buffer(2, 256);
	const auto play = [&buffer](vekt::kobber::PluginProcessor& processor, std::initializer_list<juce::MidiMessage> messages)
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

TEST_CASE("Kobber poly repeated-key retrigger avoids an exceptional sample-boundary jump", "[kobber][processor][midi][declick]")
{
	vekt::kobber::PluginProcessor processor;
	setParameter(processor, vekt::kobber::parameters::performanceMode, 0.0f);
	setParameter(processor, vekt::kobber::parameters::voiceWidth, 100.0f);
	setParameter(processor, vekt::kobber::parameters::osc1Morph, 2.0f);
	setParameter(processor, vekt::kobber::parameters::osc2Level, 0.0f);
	setParameter(processor, vekt::kobber::parameters::osc3Level, 0.0f);
	setParameter(processor, vekt::kobber::parameters::filterCutoff, 12'000.0f);
	setParameter(processor, vekt::kobber::parameters::filterEnvelopeAmount, 0.0f);
	setParameter(processor, vekt::kobber::parameters::ampAttack, 0.0005f);
	setParameter(processor, vekt::kobber::parameters::ampRelease, 2.0f);
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

TEST_CASE("Kobber voice stealing avoids an exceptional sample-boundary jump", "[kobber][processor][midi][declick]")
{
	vekt::kobber::PluginProcessor processor;
	setParameter(processor, vekt::kobber::parameters::performanceMode, 0.0f);
	setParameter(processor, vekt::kobber::parameters::voiceCount, 2.0f);
	setParameter(processor, vekt::kobber::parameters::voiceWidth, 100.0f);
	setParameter(processor, vekt::kobber::parameters::osc1Morph, 2.0f);
	setParameter(processor, vekt::kobber::parameters::osc2Level, 0.0f);
	setParameter(processor, vekt::kobber::parameters::osc3Level, 0.0f);
	setParameter(processor, vekt::kobber::parameters::filterCutoff, 12'000.0f);
	setParameter(processor, vekt::kobber::parameters::filterEnvelopeAmount, 0.0f);
	setParameter(processor, vekt::kobber::parameters::ampAttack, 0.0005f);
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

TEST_CASE("Kobber modes isolate held-note stacks by MIDI channel", "[kobber][processor][midi]")
{
	vekt::kobber::PluginProcessor processor;
	setParameter(processor, vekt::kobber::parameters::performanceMode, 1.0f);
	setParameter(processor, vekt::kobber::parameters::ampRelease, 0.005f);
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

TEST_CASE("Kobber High quality oversamples synthesis and reports latency", "[kobber][processor][quality]")
{
	vekt::kobber::PluginProcessor processor;
	setParameter(processor, vekt::kobber::parameters::trackingOversampling, 1.0f);
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

TEST_CASE("Kobber High quality handles multiple note boundaries in one host block", "[kobber][processor][quality][midi]")
{
	vekt::kobber::PluginProcessor processor;
	setParameter(processor, vekt::kobber::parameters::trackingOversampling, 1.0f);
	setParameter(processor, vekt::kobber::parameters::ampRelease, 0.005f);
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

TEST_CASE("Kobber noise keeps its level and colour at every quality", "[kobber][noise][precision][slow]")
{
	// Noise only through the open filter (20 kHz), one voice at 48 kHz: the level within 0.5 dB of 1x at every FIR
	// factor and the tilt (100-300 Hz against 1.5-2.5 kHz) within 1 dB, for white and pink. With Drive +24 dB the 1x
	// ladder folds its distortion back into the band (2.0-2.4 dB more than at any oversampled factor), so driven white
	// noise is held to 2x instead, within 0.5 dB. Measured up to 2 kHz, where the ladder's response is the same at every rate: above it the 1x ladder,
	// its 20 kHz cutoff warped against Nyquist, stays flatter than the oversampled one (1 dB at 5 kHz, 4 dB at 10 kHz),
	// a filter difference, not a noise one. Before noise was held at the host rate, white fell 4.7-14.2 dB from 2x to
	// 16x and pink's tilt moved up to 12 dB. Welch spectrum, 4096-point Hann segments, 50 % overlap, 1 s after 0.1 s.
	constexpr double rate = 48'000.0;
	constexpr int blockSize = 256;
	struct Bands { double total {}, low {}, high {}, top {}; };
	const auto measure = [&](float quality, float noiseType, float drive, float unison)
	{
		vekt::kobber::PluginProcessor processor;
		initializeDryVoice(processor);
		setParameter(processor, vekt::kobber::parameters::unison, unison);
		setParameter(processor, vekt::kobber::parameters::osc1Level, 0.0f);
		setParameter(processor, vekt::kobber::parameters::noiseType, noiseType);
		setParameter(processor, vekt::kobber::parameters::noiseLevel, 50.0f);
		setParameter(processor, vekt::kobber::parameters::filterDrive, drive);
		setParameter(processor, vekt::kobber::parameters::trackingOversampling, quality);
		processor.prepareToPlay(rate, blockSize);
		const auto settle = static_cast<int>(0.1 * rate), length = static_cast<int>(1.0 * rate);
		std::vector<float> output;
		juce::AudioBuffer<float> buffer(2, blockSize);
		juce::MidiBuffer note;
		note.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
		for (int rendered = 0; rendered < settle + length; rendered += blockSize)
		{
			renderBlock(processor, buffer, note);
			note.clear();
			for (int sample = 0; sample < blockSize; ++sample)
				if (rendered + sample >= settle) output.push_back(buffer.getSample(0, sample));
		}
		constexpr int order = 12, size = 1 << order;
		juce::dsp::FFT fft(order);
		std::vector<double> power(size / 2 + 1);
		std::vector<float> frame(2 * size);
		int segments {};
		for (std::size_t start = 0; start + size <= output.size(); start += size / 2, ++segments)
		{
			std::fill(frame.begin(), frame.end(), 0.0f);
			for (int index = 0; index < size; ++index)
				frame[static_cast<std::size_t>(index)] = output[start + static_cast<std::size_t>(index)]
					* static_cast<float>(0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * index / size));
			fft.performFrequencyOnlyForwardTransform(frame.data(), true);
			for (std::size_t bin = 0; bin < power.size(); ++bin) power[bin] += static_cast<double>(frame[bin]) * frame[bin];
		}
		Bands bands;
		for (std::size_t bin = 0; bin < power.size(); ++bin)
		{
			const auto hz = static_cast<double>(bin) * rate / size;
			if (hz >= 20.0 && hz <= 2'000.0) bands.total += power[bin];
			if (hz >= 100.0 && hz <= 300.0) bands.low += power[bin];
			if (hz >= 1'500.0 && hz <= 2'500.0) bands.high += power[bin];
			if (hz >= 10'000.0 && hz <= 20'000.0) bands.top += power[bin];
		}
		return bands;
	};
	const auto db = [](double ratio) { return 10.0 * std::log10(ratio); };
	// Reference quality: Off (1x), or 2x FIR for driven noise; then 2x, 4x, 8x and 16x FIR. Pink also with Unison 4x,
	// whose layers hold their own noise. Above 2 kHz (`top`, 10-20 kHz) the factors from 4x up agree within 0.5 dB.
	for (const auto& [noiseType, drive, referenceQuality, unison] : { std::tuple { 1.0f, 0.0f, 0.0f, 0.0f },
			 std::tuple { 2.0f, 0.0f, 0.0f, 0.0f }, std::tuple { 2.0f, 0.0f, 0.0f, 2.0f }, std::tuple { 1.0f, 24.0f, 3.0f, 0.0f } })
	{
		const auto reference = measure(referenceQuality, noiseType, drive, unison);
		double topAt4x {};
		for (const auto quality : { 3.0f, 4.0f, 5.0f, 6.0f })
		{
			if (juce::exactlyEqual(quality, referenceQuality)) continue;
			const auto bands = measure(quality, noiseType, drive, unison);
			const auto level = db(bands.total / reference.total);
			const auto tilt = db((bands.low / bands.high) / (reference.low / reference.high));
			const auto top = db(bands.top / reference.top); // for information: includes the filter difference
			CAPTURE(noiseType, drive, unison, quality, level, tilt, top);
			CHECK(std::abs(level) < 0.5);
			CHECK(std::abs(tilt) < 1.0);
			if (quality == 4.0f) topAt4x = top;
			else if (quality > 4.0f) CHECK(std::abs(top - topAt4x) < 0.5);
		}
	}
}

TEST_CASE("Kobber coupled ladder solves a sustained resonant chord at the highest internal rate", "[kobber][processor][ladder-coupled][quality][precision][slow]")
{
	// 192 kHz host at 16x (vekt::dsp::maximumInternalSampleRate): four voices, full Resonance, +24 dB Drive, a
	// cutoff sweep from 200 Hz to 8 kHz; every solve converges and nothing goes non-finite.
	constexpr auto blockSize = 256;
	vekt::kobber::PluginProcessor processor;
	initializeDryVoice(processor);
	setParameter(processor, vekt::kobber::parameters::trackingOversampling, 6.0f); // 16x FIR
	setParameter(processor, vekt::kobber::parameters::filterResonance, 100.0f);
	setParameter(processor, vekt::kobber::parameters::filterDrive, 24.0f);
	setParameter(processor, vekt::kobber::parameters::filterCutoff, 200.0f);
	processor.prepareToPlay(vekt::dsp::maximumHostSampleRate, blockSize);
	REQUIRE(processor.getActiveQuality().multiplier() == vekt::dsp::maximumOversamplingFactor);
	juce::AudioBuffer<float> buffer(2, blockSize);
	juce::MidiBuffer chord;
	for (const auto note : { 36, 43, 48, 55 }) chord.addEvent(juce::MidiMessage::noteOn(1, note, 0.9f), 0);
	constexpr int blocks = 150; // 0.2 s
	float energy {};
	for (int block = 0; block < blocks; ++block)
	{
		const auto position = static_cast<float>(block) / static_cast<float>(blocks - 1);
		setParameter(processor, vekt::kobber::parameters::filterCutoff, 200.0f * std::pow(40.0f, position));
		renderBlock(processor, buffer, block == 0 ? chord : juce::MidiBuffer {});
		for (int channel = 0; channel < 2; ++channel)
			for (int sample = 0; sample < blockSize; ++sample)
			{
				REQUIRE(std::isfinite(buffer.getSample(channel, sample)));
				energy += std::abs(buffer.getSample(channel, sample));
			}
	}
	REQUIRE(energy > 1.0f);
	const auto work = processor.coupledWorkSnapshot();
	CAPTURE(work.samples, work.iterations, work.lineSearchTrials);
	REQUIRE(work.samples > 0);
	REQUIRE(work.unconverged == 0);
	REQUIRE(work.nonFinite == 0);
}

TEST_CASE("Kobber applies an Offline quality change at the next block of an offline render", "[kobber][processor][quality]")
{
	vekt::kobber::PluginProcessor processor;
	processor.setNonRealtime(true);
	processor.prepareToPlay(48'000.0, 128);
	REQUIRE(processor.getActiveQuality() == vekt::dsp::offlineQualityFrom(2.0f)); // the 4x FIR default
	juce::AudioBuffer<float> buffer(2, 128);
	juce::MidiBuffer midi;
	midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
	processor.processBlock(buffer, midi);
	setParameter(processor, vekt::kobber::parameters::offlineOversampling, 4.0f); // 16x FIR
	setParameter(processor, vekt::kobber::parameters::trackingOversampling, 5.0f); // not used while offline
	midi.clear();
	processor.processBlock(buffer, midi);
	REQUIRE(processor.getActiveQuality() == vekt::dsp::offlineQualityFrom(4.0f));
	vekt::dsp::OversamplingBank<float> expected(2);
	expected.prepare(128);
	expected.activate(vekt::dsp::offlineQualityFrom(4.0f));
	REQUIRE(processor.getLatencySamples() == expected.getActiveLatencySamples());
	REQUIRE(processor.getLatencyDisplay() == expected.getActiveLatencySamples());
	midi.addEvent(juce::MidiMessage::noteOn(1, 64, 0.8f), 0);
	float energy {};
	for (int block = 0; block < 4; ++block)
	{
		processor.processBlock(buffer, midi);
		midi.clear();
		for (int channel = 0; channel < 2; ++channel)
			for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
			{
				REQUIRE(std::isfinite(buffer.getSample(channel, sample)));
				energy += std::abs(buffer.getSample(channel, sample));
			}
	}
	REQUIRE(energy > 1.0e-3f);
}

TEST_CASE("Kobber quality choices activate distinct oversampling paths and latency", "[kobber][processor][quality]")
{
	// Every shared Tracking choice in real time and every Offline choice when the host renders offline (ADR 0010).
	vekt::dsp::OversamplingBank<float> expected(2);
	expected.prepare(257);
	for (const bool offline : { false, true })
	for (int index = 0; index < (offline ? vekt::dsp::offlineQualityChoices() : vekt::dsp::trackingQualityChoices()).size(); ++index)
	{
		vekt::kobber::PluginProcessor processor;
		setParameter(processor, offline ? vekt::kobber::parameters::offlineOversampling : vekt::kobber::parameters::trackingOversampling,
			static_cast<float>(index));
		processor.setNonRealtime(offline);
		processor.prepareToPlay(48'000.0, 257);
		const auto quality = offline ? vekt::dsp::offlineQualityFrom(static_cast<float>(index))
			: vekt::dsp::trackingQualityFrom(static_cast<float>(index));
		expected.activate(quality);
		CAPTURE(offline, index, expected.getActiveFactor(), expected.getActiveLatencySamples());
		REQUIRE(processor.getActiveQuality() == quality);
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

TEST_CASE("Kobber unison spread changes stereo rendering", "[kobber][processor][unison]")
{
	vekt::kobber::PluginProcessor centered, spread;
	for (auto* processor : { &centered, &spread })
	{
		setParameter(*processor, vekt::kobber::parameters::performanceMode, 1.0f);
		setParameter(*processor, vekt::kobber::parameters::unison, 2.0f);
		setParameter(*processor, vekt::kobber::parameters::unisonDetune, 20.0f);
		setParameter(*processor, vekt::kobber::parameters::unisonSpread, 0.0f);
		setParameter(*processor, vekt::kobber::parameters::voiceWidth, 0.0f);
		setParameter(*processor, vekt::kobber::parameters::osc2Level, 0.0f);
		setParameter(*processor, vekt::kobber::parameters::osc3Level, 0.0f);
		processor->prepareToPlay(48'000.0, 512);
	}
	setParameter(spread, vekt::kobber::parameters::unisonSpread, 100.0f);
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

TEST_CASE("Kobber voice pan controls round-robin stereo mix", "[kobber][processor][stereo]")
{
	vekt::kobber::PluginProcessor centered, panned;
	for (auto* processor : { &centered, &panned })
	{
		setParameter(*processor, vekt::kobber::parameters::performanceMode, 0.0f);
		setParameter(*processor, vekt::kobber::parameters::unison, 0.0f);
		setParameter(*processor, vekt::kobber::parameters::unisonSpread, 0.0f);
		setParameter(*processor, vekt::kobber::parameters::osc2Level, 0.0f);
		setParameter(*processor, vekt::kobber::parameters::osc3Level, 0.0f);
		processor->prepareToPlay(48'000.0, 512);
	}
	setParameter(centered, vekt::kobber::parameters::voiceWidth, 0.0f);
	setParameter(panned, vekt::kobber::parameters::voiceWidth, 100.0f);
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

TEST_CASE("Kobber rendering is deterministic with drift enabled", "[kobber][processor][determinism]")
{
	vekt::kobber::PluginProcessor first, second;
	for (auto* processor : { &first, &second })
	{
		setParameter(*processor, vekt::kobber::parameters::drift, 100.0f);
		setParameter(*processor, vekt::kobber::parameters::osc1Morph, 2.0f);
		setParameter(*processor, vekt::kobber::parameters::osc2Level, 0.0f);
		setParameter(*processor, vekt::kobber::parameters::osc3Level, 0.0f);
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

TEST_CASE("Kobber processor and extracted voice render identically", "[kobber][processor][engine][determinism]")
{
	// The Ladder at LP, and at Mode 0.5, where each unison layer also runs its high-pass ladder (ADR 0009).
	const auto mode = GENERATE(-1.0f, 0.5f);
	vekt::kobber::PluginProcessor processor;
	for (auto* parameter : processor.juce::AudioProcessor::getParameters())
		parameter->setValueNotifyingHost(parameter->getDefaultValue());
	setParameter(processor, vekt::kobber::parameters::filterMode, mode);

	vekt::kobber::KobberVoiceSettings settings {
		.rangeOctaves = { 1, 0, 2 }, // 4', 8', 2'
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
		.glideMode = vekt::kobber::GlideMode::off,
		.noiseType = vekt::kobber::NoiseType::white,
		.qCompensation = true
	};

	const std::array ranges { vekt::kobber::parameters::osc1Range, vekt::kobber::parameters::osc2Range, vekt::kobber::parameters::osc3Range };
	const std::array semitones { vekt::kobber::parameters::osc1Semitone, vekt::kobber::parameters::osc2Semitone, vekt::kobber::parameters::osc3Semitone };
	const std::array fines { vekt::kobber::parameters::osc1Fine, vekt::kobber::parameters::osc2Fine, vekt::kobber::parameters::osc3Fine };
	const std::array octaves { vekt::kobber::parameters::osc1Octave, vekt::kobber::parameters::osc2Octave, vekt::kobber::parameters::osc3Octave };
	const std::array levels { vekt::kobber::parameters::osc1Level, vekt::kobber::parameters::osc2Level, vekt::kobber::parameters::osc3Level };
	const std::array morphs { vekt::kobber::parameters::osc1Morph, vekt::kobber::parameters::osc2Morph, vekt::kobber::parameters::osc3Morph };
	const std::array widths { vekt::kobber::parameters::osc1PulseWidth, vekt::kobber::parameters::osc2PulseWidth, vekt::kobber::parameters::osc3PulseWidth };
	for (std::size_t oscillator = 0; oscillator < 3; ++oscillator)
	{
		setParameter(processor, ranges[oscillator], static_cast<float>(vekt::kobber::oscillatorRanges.indexOf(settings.rangeOctaves[oscillator])));
		setParameter(processor, semitones[oscillator], settings.semitone[oscillator]);
		setParameter(processor, fines[oscillator], settings.fine[oscillator]);
		setParameter(processor, octaves[oscillator], settings.octave[oscillator]);
		setParameter(processor, levels[oscillator], settings.level[oscillator] * 100.0f);
		setParameter(processor, morphs[oscillator], settings.morph[oscillator]);
		setParameter(processor, widths[oscillator], settings.pulseWidth[oscillator]);
	}
	setParameter(processor, vekt::kobber::parameters::performanceMode, 1.0f);
	setParameter(processor, vekt::kobber::parameters::trackingOversampling, 0.0f);
	setParameter(processor, vekt::kobber::parameters::unison, 1.0f);
	setParameter(processor, vekt::kobber::parameters::unisonDetune, settings.detune);
	setParameter(processor, vekt::kobber::parameters::unisonSpread, settings.unisonSpread * 100.0f);
	setParameter(processor, vekt::kobber::parameters::voiceWidth, settings.voiceWidth * 100.0f);
	setParameter(processor, vekt::kobber::parameters::glideMode, static_cast<float>(vekt::kobber::glideModes.indexOf(settings.glideMode)));
	setParameter(processor, vekt::kobber::parameters::glideTime, settings.glideTime);
	setParameter(processor, vekt::kobber::parameters::calibration, settings.calibration);
	setParameter(processor, vekt::kobber::parameters::drift, settings.drift);
	setParameter(processor, vekt::kobber::parameters::masterOutput, 0.0f);
	setParameter(processor, vekt::kobber::parameters::noiseType, static_cast<float>(vekt::kobber::noiseTypes.indexOf(settings.noiseType)));
	setParameter(processor, vekt::kobber::parameters::noiseLevel, settings.noiseLevel * 100.0f);
	setParameter(processor, vekt::kobber::parameters::filterCutoff, settings.cutoff);
	setParameter(processor, vekt::kobber::parameters::filterResonance, settings.resonance * 100.0f);
	setParameter(processor, vekt::kobber::parameters::filterKeyTracking, settings.tracking * 100.0f);
	setParameter(processor, vekt::kobber::parameters::filterEnvelopeAmount, settings.envelopeAmount * 100.0f);
	setParameter(processor, vekt::kobber::parameters::filterDrive, settings.drive);
	setParameter(processor, vekt::kobber::parameters::filterQCompensation, 1.0f);
	setParameter(processor, vekt::kobber::parameters::ampAttack, settings.ampAttack);
	setParameter(processor, vekt::kobber::parameters::ampDecay, settings.ampDecay);
	setParameter(processor, vekt::kobber::parameters::ampSustain, settings.ampSustain * 100.0f);
	setParameter(processor, vekt::kobber::parameters::ampRelease, settings.ampRelease);
	setParameter(processor, vekt::kobber::parameters::filterAttack, settings.filterAttack);
	setParameter(processor, vekt::kobber::parameters::filterDecay, settings.filterDecay);
	setParameter(processor, vekt::kobber::parameters::filterSustain, settings.filterSustain * 100.0f);
	setParameter(processor, vekt::kobber::parameters::filterRelease, settings.filterRelease);
	setParameter(processor, vekt::kobber::parameters::ampVelocity, settings.ampVelocity * 100.0f);
	setParameter(processor, vekt::kobber::parameters::filterVelocity, settings.filterVelocity * 100.0f);

	// The processor's mapping (voiceSettingsFrom, at the default tempo) must give back, in the voice's units, what was set
	// above, field by field; the voice then renders with exactly what the processor uses.
	const auto mapped = vekt::kobber::voiceSettingsFrom(vekt::kobber::KobberParameterValues::resolve(processor.getParameters()), 120.0);
	for (std::size_t oscillator = 0; oscillator < 3; ++oscillator)
	{
		REQUIRE(mapped.rangeOctaves[oscillator] == settings.rangeOctaves[oscillator]);
		REQUIRE(mapped.semitone[oscillator] == Catch::Approx(settings.semitone[oscillator]).margin(1.0e-4));
		REQUIRE(mapped.fine[oscillator] == Catch::Approx(settings.fine[oscillator]).margin(1.0e-4));
		REQUIRE(mapped.octave[oscillator] == Catch::Approx(settings.octave[oscillator]).margin(1.0e-4));
		REQUIRE(mapped.level[oscillator] == Catch::Approx(settings.level[oscillator]).margin(1.0e-4));
		REQUIRE(mapped.morph[oscillator] == Catch::Approx(settings.morph[oscillator]).margin(1.0e-4));
		REQUIRE(mapped.pulseWidth[oscillator] == Catch::Approx(settings.pulseWidth[oscillator]).margin(1.0e-4));
	}
	REQUIRE(mapped.noiseLevel == Catch::Approx(settings.noiseLevel).margin(1.0e-4));
	REQUIRE(mapped.cutoff == Catch::Approx(settings.cutoff).margin(1.0e-4));
	REQUIRE(mapped.resonance == Catch::Approx(settings.resonance).margin(1.0e-4));
	REQUIRE(mapped.tracking == Catch::Approx(settings.tracking).margin(1.0e-4));
	REQUIRE(mapped.envelopeAmount == Catch::Approx(settings.envelopeAmount).margin(1.0e-4));
	REQUIRE(mapped.drive == Catch::Approx(settings.drive).margin(1.0e-4));
	REQUIRE(mapped.ampAttack == Catch::Approx(settings.ampAttack).margin(1.0e-4));
	REQUIRE(mapped.ampDecay == Catch::Approx(settings.ampDecay).margin(1.0e-4));
	REQUIRE(mapped.ampSustain == Catch::Approx(settings.ampSustain).margin(1.0e-4));
	REQUIRE(mapped.ampRelease == Catch::Approx(settings.ampRelease).margin(1.0e-4));
	REQUIRE(mapped.filterAttack == Catch::Approx(settings.filterAttack).margin(1.0e-4));
	REQUIRE(mapped.filterDecay == Catch::Approx(settings.filterDecay).margin(1.0e-4));
	REQUIRE(mapped.filterSustain == Catch::Approx(settings.filterSustain).margin(1.0e-4));
	REQUIRE(mapped.filterRelease == Catch::Approx(settings.filterRelease).margin(1.0e-4));
	REQUIRE(mapped.ampVelocity == Catch::Approx(settings.ampVelocity).margin(1.0e-4));
	REQUIRE(mapped.filterVelocity == Catch::Approx(settings.filterVelocity).margin(1.0e-4));
	REQUIRE(mapped.calibration == Catch::Approx(settings.calibration).margin(1.0e-4));
	REQUIRE(mapped.detune == Catch::Approx(settings.detune).margin(1.0e-4));
	REQUIRE(mapped.unisonSpread == Catch::Approx(settings.unisonSpread).margin(1.0e-4));
	REQUIRE(mapped.voiceWidth == Catch::Approx(settings.voiceWidth).margin(1.0e-4));
	REQUIRE(mapped.glideTime == Catch::Approx(settings.glideTime).margin(1.0e-4));
	REQUIRE(mapped.drift == Catch::Approx(settings.drift).margin(1.0e-4));
	REQUIRE(mapped.unison == settings.unison);
	REQUIRE(mapped.glideMode == settings.glideMode);
	REQUIRE(mapped.noiseType == settings.noiseType);
	REQUIRE(mapped.qCompensation);
	REQUIRE(mapped.filterMode == Catch::Approx(mode).margin(1.0e-4));
	settings = mapped;

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

	vekt::kobber::KobberVoice voice;
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

TEST_CASE("Kobber PolyBLEP oscillator output remains finite across supported rates", "[kobber][processor][matrix]")
{
	for (const auto rate : { 44'100.0, 48'000.0, 96'000.0, 192'000.0 })
		for (const auto morph : { 2.0f, 3.0f })
		{
			vekt::kobber::PluginProcessor processor;
			setParameter(processor, vekt::kobber::parameters::osc1Morph, morph);
			setParameter(processor, vekt::kobber::parameters::osc1Level, 100.0f);
			setParameter(processor, vekt::kobber::parameters::osc2Level, 0.0f);
			setParameter(processor, vekt::kobber::parameters::osc3Level, 0.0f);
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

TEST_CASE("Kobber handles duplicate notes and channel panic messages without stuck output", "[kobber][processor][midi]")
{
	vekt::kobber::PluginProcessor processor;
	setParameter(processor, vekt::kobber::parameters::performanceMode, 1.0f);
	setParameter(processor, vekt::kobber::parameters::ampRelease, 0.005f);
	// MIDI logic only: at 1x the output is exactly silent after All Sound Off (no decimator tail).
	setParameter(processor, vekt::kobber::parameters::trackingOversampling, 0.0f);
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

TEST_CASE("Kobber Multicore renders exactly the same samples as single-threaded rendering", "[kobber][processor][multicore]")
{
	// Work units and their summing order do not depend on threads, and a voice renders the same bits in any job,
	// so switching Multicore must not change a single sample: every filter type, 1 to 16 voices (fewer units than
	// threads splits them into smaller jobs), every unison setting, 1x and 2x, notes ending inside blocks and LFO
	// movement.
	for (const auto filter : { 0, 1, 2 }) // Ladder, SVF, K35
	for (const auto unison : { 0.0f, 1.0f, 2.0f })
		for (const auto quality : { 0.0f, 1.0f })
		{
			CAPTURE(filter, unison, quality);
			vekt::kobber::PluginProcessor single, multi;
			for (auto* processor : { &single, &multi })
			{
				setParameter(*processor, vekt::kobber::parameters::filterType, static_cast<float>(filter));
				setParameter(*processor, vekt::kobber::parameters::voiceCount, 4.0f); // 16 voices
				setParameter(*processor, vekt::kobber::parameters::unison, unison);
				setParameter(*processor, vekt::kobber::parameters::trackingOversampling, quality);
				setParameter(*processor, vekt::kobber::parameters::ampRelease, 0.02f);
				setParameter(*processor, vekt::kobber::parameters::lfos[0].width[0], 30.0f);
				setParameter(*processor, vekt::kobber::parameters::lfos[0].rate, 7.0f);
			}
			setParameter(multi, vekt::kobber::parameters::multicore, 1.0f);
			single.prepareToPlay(48'000.0, 256);
			multi.prepareToPlay(48'000.0, 256);
			REQUIRE(single.getRenderHelperCount() == 0);
			REQUIRE(multi.getRenderHelperCount() == vekt::kobber::KobberRenderWorkers::defaultThreadCount());
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

TEST_CASE("Kobber Multicore helpers are created once when prepare and enabling overlap", "[kobber][processor][multicore]")
{
	// prepareToPlay (a host thread) and switching Multicore on (the message thread) can both create the pool.
	juce::ScopedJuceInitialiser_GUI messageThread;
	for (int attempt = 0; attempt < 20; ++attempt)
	{
		vekt::kobber::PluginProcessor processor;
		std::atomic<bool> go {};
		std::thread host([&]
		{
			while (!go.load()) std::this_thread::yield();
			processor.prepareToPlay(48'000.0, 128);
		});
		go.store(true);
		setParameter(processor, vekt::kobber::parameters::multicore, 1.0f);
		host.join();
		processor.prepareToPlay(48'000.0, 128); // creates the pool if the host thread saw Multicore still off
		REQUIRE(processor.getRenderHelperCount() == vekt::kobber::KobberRenderWorkers::defaultThreadCount());
	}
}

TEST_CASE("Kobber Multicore keeps rendering while the host workgroup changes", "[kobber][processor][multicore]")
{
	// The workgroup arrives on the render thread and must never wait for a helper; rendering continues and
	// still matches single-threaded output.
	vekt::kobber::PluginProcessor single, multi;
	for (auto* processor : { &single, &multi })
		setParameter(*processor, vekt::kobber::parameters::voiceCount, 4.0f);
	setParameter(multi, vekt::kobber::parameters::multicore, 1.0f);
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
