#include <vekt/mono/PluginProcessor.h>

#include "MonoVoice.h"

#include <vekt/audio_analysis/Measurements.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <set>

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

TEST_CASE("Mono Q compensation is bounded output gain outside saturation", "[mono][processor][filter][qcomp]")
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
						setParameter(*processor, vekt::mono::parameters::osc1Level, emphasis == 100.0f ? 0.0f : 100.0f);
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
					const auto expectedGain = std::min(3.9810717f, 1.0f + 4.0f * std::pow(emphasis * 0.01f, 0.72f));
					INFO("rate=" << sampleRate << ", quality=" << quality << ", emphasis=" << emphasis << ", drive=" << drive);
					REQUIRE(rms(dryBuffer) > 0.0001f);
					REQUIRE(rms(wetBuffer) / rms(dryBuffer) == Catch::Approx(expectedGain).margin(0.0001f));
					REQUIRE(dry.getLatencySamples() == compensated.getLatencySamples());
					for (int sample = 0; sample < dryBuffer.getNumSamples(); ++sample)
						REQUIRE(wetBuffer.getSample(0, sample) == Catch::Approx(dryBuffer.getSample(0, sample) * expectedGain).margin(2.0e-5f));
				}
}

TEST_CASE("Mono Q compensation ramps without changing the held voice", "[mono][processor][filter][qcomp]")
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
	const auto maximumGain = 1.0f + 4.0f * std::pow(0.5f, 0.72f);
	for (const auto enabled : { true, false })
	{
		setParameter(switched, vekt::mono::parameters::filterQCompensation, enabled ? 1.0f : 0.0f);
		renderBlock(dry, dryBuffer);
		renderBlock(switched, switchedBuffer);
		for (int sample = 0; sample < 2400; ++sample)
		{
			const auto progress = std::min(1.0f, static_cast<float>(sample + 1) / 960.0f);
			const auto gain = enabled ? 1.0f + (maximumGain - 1.0f) * progress
				: maximumGain + (1.0f - maximumGain) * progress;
			REQUIRE(switchedBuffer.getSample(0, sample) == Catch::Approx(dryBuffer.getSample(0, sample) * gain).margin(2.0e-5f));
		}
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

#if defined(VEKT_MONO_LADDER_DEVELOPMENT)
TEST_CASE("Mono development ladder is explicit and restricted to 1x", "[mono][processor][ladder-development]")
{
	vekt::mono::PluginProcessor legacy, rerun(true), oversampled(true), higherLegacy;
	vekt::mono::PluginProcessor enabled(true);
	REQUIRE(legacy.getName() == "Vekt Mono");
	REQUIRE(enabled.getName() == "Vekt Mono Ladder Preview");
	REQUIRE_FALSE(legacy.isDevelopmentLadderActive());
	for (auto* processor : { &legacy, &rerun, &oversampled, &higherLegacy, &enabled })
	{
		initializeDryVoice(*processor);
		setParameter(*processor, vekt::mono::parameters::filterCutoff, 1'000.0f);
		setParameter(*processor, vekt::mono::parameters::filterResonance, 85.0f);
		setParameter(*processor, vekt::mono::parameters::filterDrive, 12.0f);
	}
	setParameter(oversampled, vekt::mono::parameters::quality, 3.0f);
	setParameter(higherLegacy, vekt::mono::parameters::quality, 3.0f);
	for (auto* processor : { &legacy, &rerun, &oversampled, &higherLegacy, &enabled })
		processor->prepareToPlay(48'000.0, 128);
	REQUIRE_FALSE(legacy.isDevelopmentLadderActive());
	REQUIRE_FALSE(oversampled.isDevelopmentLadderActive());
	REQUIRE(enabled.isDevelopmentLadderActive());
	REQUIRE(rerun.isDevelopmentLadderActive());
	REQUIRE(enabled.getLatencySamples() == 0);
	REQUIRE(oversampled.getActiveQuality() == 3);
	REQUIRE(oversampled.getLatencySamples() == higherLegacy.getLatencySamples());
	juce::AudioBuffer<float> first(2, 128), second(2, 128), baseline(2, 128);
	juce::AudioBuffer<float> high(2, 128), highBaseline(2, 128);
	juce::MidiBuffer note;
	note.addEvent(juce::MidiMessage::noteOn(1, 48, 0.8f), 0);
	for (int block = 0; block < 8; ++block)
	{
		renderBlock(enabled, first, note);
		renderBlock(rerun, second, note);
		renderBlock(legacy, baseline, note);
		renderBlock(oversampled, high, note);
		renderBlock(higherLegacy, highBaseline, note);
		if (block == 0) note.clear();
		for (int channel = 0; channel < 2; ++channel)
			for (int sample = 0; sample < 128; ++sample)
			{
				const auto a = first.getSample(channel, sample);
				const auto b = second.getSample(channel, sample);
				REQUIRE(std::isfinite(a));
				REQUIRE(a == Catch::Approx(b).margin(0.0f));
				REQUIRE(std::bit_cast<std::uint32_t>(high.getSample(channel, sample))
					== std::bit_cast<std::uint32_t>(highBaseline.getSample(channel, sample)));
			}
		if (block == 7) REQUIRE(std::abs(first.getSample(0, 96) - baseline.getSample(0, 96)) > 1.0e-5f);
	}
}

TEST_CASE("Mono development ladder stays silent and finite across driven notes", "[mono][processor][ladder-development]")
{
	for (const auto rate : { 44'100.0, 48'000.0, 96'000.0 })
	{
		vekt::mono::PluginProcessor processor(true);
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
		REQUIRE(processor.isDevelopmentLadderActive());
		renderBlock(processor, buffer);
		for (int channel = 0; channel < 2; ++channel)
			for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
				REQUIRE(std::bit_cast<std::uint32_t>(buffer.getSample(channel, sample))
					== std::bit_cast<std::uint32_t>(0.0f));
	}
}

TEST_CASE("Mono coupled solver is development-only and leaves nested mode unchanged", "[mono][processor][ladder-coupled]")
{
	vekt::mono::PluginProcessor nested(true), coupled(true, true), legacy;
	for (auto* processor : { &nested, &coupled, &legacy })
	{
		initializeDryVoice(*processor);
		setParameter(*processor, vekt::mono::parameters::filterCutoff, 1'000.0f);
		setParameter(*processor, vekt::mono::parameters::filterResonance, 85.0f);
		setParameter(*processor, vekt::mono::parameters::filterDrive, 12.0f);
		processor->prepareToPlay(48'000.0, 128);
	}
	REQUIRE(nested.isDevelopmentLadderActive());
	REQUIRE_FALSE(nested.isCoupledLadderActive());
	REQUIRE(coupled.isCoupledLadderActive());
	REQUIRE_FALSE(legacy.isDevelopmentLadderActive());
	REQUIRE(nested.coupledWorkSnapshot().samples == 0);
	juce::AudioBuffer<float> first(2, 128), second(2, 128);
	juce::MidiBuffer note;
	note.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
	for (int block = 0; block < 8; ++block)
	{
		renderBlock(nested, first, note);
		renderBlock(coupled, second, note);
		note.clear();
		for (int channel = 0; channel < 2; ++channel)
			for (int sample = 0; sample < 128; ++sample)
			{
				const auto a = first.getSample(channel, sample);
				const auto b = second.getSample(channel, sample);
				REQUIRE(std::isfinite(a));
				REQUIRE(std::isfinite(b));
				REQUIRE(std::abs(a - b) < 1.0e-4f);
			}
	}
	const auto work = coupled.coupledWorkSnapshot();
	REQUIRE(work.samples > 0);
	REQUIRE(work.iterations > 0);
	REQUIRE(work.lineSearchTrials >= work.iterations);
	REQUIRE(work.unconverged == 0);
	REQUIRE(work.nonFinite == 0);
}

TEST_CASE("Mono coupled preview exercises every retained quality", "[mono][processor][ladder-coupled][quality]")
{
	for (int quality = 0; quality <= 3; ++quality)
	{
		vekt::mono::PluginProcessor coupled(true, true), nested(true), legacy;
		for (auto* processor : { &coupled, &nested, &legacy })
		{
			initializeDryVoice(*processor);
			setParameter(*processor, vekt::mono::parameters::quality, static_cast<float>(quality));
			setParameter(*processor, vekt::mono::parameters::filterCutoff, 1'000.0f);
			setParameter(*processor, vekt::mono::parameters::filterResonance, 85.0f);
			setParameter(*processor, vekt::mono::parameters::filterDrive, 12.0f);
			processor->prepareToPlay(48'000.0, 128);
		}
		INFO("quality=" << quality);
		REQUIRE(coupled.getActiveQuality() == quality);
		REQUIRE(coupled.getLatencySamples() == legacy.getLatencySamples());
		REQUIRE(coupled.isDevelopmentLadderActive());
		REQUIRE(coupled.isCoupledLadderActive());
		REQUIRE(nested.isDevelopmentLadderActive() == (quality == 0));
		REQUIRE_FALSE(nested.isCoupledLadderActive());
		REQUIRE_FALSE(legacy.isDevelopmentLadderActive());
		juce::AudioBuffer<float> actual(2, 128), baseline(2, 128);
		juce::MidiBuffer note;
		note.addEvent(juce::MidiMessage::noteOn(1, 48, 0.8f), 0);
		for (int block = 0; block < 4; ++block)
		{
			renderBlock(coupled, actual, note);
			renderBlock(legacy, baseline, note);
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

TEST_CASE("Mono coupled quality state recalls without selecting coupled in the normal processor", "[mono][processor][ladder-coupled][quality][state]")
{
	for (int quality = 0; quality <= 3; ++quality)
	{
		vekt::mono::PluginProcessor source(true, true), restored(true, true), legacy;
		initializeDryVoice(source);
		setParameter(source, vekt::mono::parameters::quality, static_cast<float>(quality));
		setParameter(source, vekt::mono::parameters::filterCutoff, 1'000.0f);
		setParameter(source, vekt::mono::parameters::filterResonance, 85.0f);
		setParameter(source, vekt::mono::parameters::filterDrive, 12.0f);
		juce::MemoryBlock state;
		source.getStateInformation(state);
		REQUIRE(state.getSize() > 0);
		restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
		legacy.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
		for (auto* processor : { &source, &restored, &legacy })
			processor->prepareToPlay(48'000.0, 128);
		INFO("quality=" << quality);
		REQUIRE(source.getActiveQuality() == quality);
		REQUIRE(restored.getActiveQuality() == quality);
		REQUIRE(legacy.getActiveQuality() == quality);
		REQUIRE(source.getLatencySamples() == restored.getLatencySamples());
		REQUIRE(source.getLatencySamples() == legacy.getLatencySamples());
		REQUIRE(source.isCoupledLadderActive());
		REQUIRE(restored.isCoupledLadderActive());
		REQUIRE_FALSE(legacy.isDevelopmentLadderActive());
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
		REQUIRE(legacy.coupledWorkSnapshot().samples == 0);
	}
}

TEST_CASE("Mono coupled and nested callbacks agree across MIDI and control boundaries", "[mono][processor][ladder-coupled]")
{
	for (const auto rate : { 44'100.0, 48'000.0 })
		for (const auto blockSize : { 128, 257 })
		{
			vekt::mono::PluginProcessor nested(true), coupled(true, true);
			for (auto* processor : { &nested, &coupled })
			{
				initializeDryVoice(*processor);
				setParameter(*processor, vekt::mono::parameters::performanceMode, 0.0f);
				setParameter(*processor, vekt::mono::parameters::filterCutoff, 1'000.0f);
				setParameter(*processor, vekt::mono::parameters::filterResonance, 85.0f);
				setParameter(*processor, vekt::mono::parameters::filterDrive, 12.0f);
				processor->prepareToPlay(rate, blockSize);
			}
			REQUIRE_FALSE(nested.isCoupledLadderActive());
			REQUIRE(coupled.isCoupledLadderActive());
			REQUIRE(nested.getLatencySamples() == coupled.getLatencySamples());
			juce::AudioBuffer<float> baseline(2, blockSize), actual(2, blockSize);
			double maximumDifference {}, energy {};
			for (int block = 0; block < 24; ++block)
			{
				if (block == 4 || block == 12 || block == 18)
					for (auto* processor : { &nested, &coupled })
					{
						setParameter(*processor, vekt::mono::parameters::filterCutoff,
							block == 4 ? 20'000.0f : block == 12 ? 20.0f : 1'000.0f);
						setParameter(*processor, vekt::mono::parameters::filterResonance,
							block == 12 ? 100.0f : 85.0f);
						setParameter(*processor, vekt::mono::parameters::filterDrive,
							block == 4 ? 24.0f : block == 12 ? 0.0f : 12.0f);
						}
				juce::MidiBuffer midi;
				if (block == 0) midi.addEvent(juce::MidiMessage::noteOn(1, 48, 0.8f), 0);
				if (block == 2) midi.addEvent(juce::MidiMessage::noteOn(1, 55, 0.7f), blockSize / 2);
				if (block == 6) midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), blockSize - 1);
				if (block == 10) midi.addEvent(juce::MidiMessage::noteOff(1, 55), blockSize / 3);
				if (block == 15) midi.addEvent(juce::MidiMessage::noteOff(1, 48), 1);
				if (block == 20) midi.addEvent(juce::MidiMessage::noteOff(1, 60), blockSize / 2);
				renderBlock(nested, baseline, midi);
				renderBlock(coupled, actual, midi);
				for (int channel = 0; channel < 2; ++channel)
					for (int sample = 0; sample < blockSize; ++sample)
					{
						const auto expected = baseline.getSample(channel, sample);
						const auto value = actual.getSample(channel, sample);
						CAPTURE(rate, blockSize, block, channel, sample, expected, value);
						REQUIRE(std::isfinite(expected));
						REQUIRE(std::isfinite(value));
						maximumDifference = std::max(maximumDifference,
							std::abs(static_cast<double>(value) - expected));
						energy += static_cast<double>(value) * value;
					}
			}
			CAPTURE(rate, blockSize, maximumDifference, energy);
			REQUIRE(energy > 0.01);
			REQUIRE(maximumDifference < 1.0e-4);
			const auto work = coupled.coupledWorkSnapshot();
			REQUIRE(work.samples > 0);
			REQUIRE(work.unconverged == 0);
			REQUIRE(work.nonFinite == 0);
			REQUIRE(nested.coupledWorkSnapshot().samples == 0);
		}
}

TEST_CASE("Mono coupled quality changes defer through sustain and retain the coupled engine", "[mono][processor][ladder-coupled][quality]")
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
	};
	for (const auto [initial, target] : { std::pair { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 } })
	{
		PlayHead playHead;
		vekt::mono::PluginProcessor coupled(true, true), legacy;
		for (auto* processor : { &coupled, &legacy })
		{
			initializeDryVoice(*processor);
			setParameter(*processor, vekt::mono::parameters::ampRelease, 0.005f);
			setParameter(*processor, vekt::mono::parameters::filterCutoff, 1'000.0f);
			setParameter(*processor, vekt::mono::parameters::filterResonance, 85.0f);
			setParameter(*processor, vekt::mono::parameters::filterDrive, 12.0f);
			setParameter(*processor, vekt::mono::parameters::quality, static_cast<float>(initial));
			processor->setPlayHead(&playHead);
			processor->prepareToPlay(48'000.0, 128);
		}
		INFO("initial=" << initial << ", target=" << target);
		const auto previousLatency = coupled.getLatencySamples();
		juce::AudioBuffer<float> actual(2, 128), baseline(2, 128);
		juce::MidiBuffer held;
		held.addEvent(juce::MidiMessage::controllerEvent(1, 64, 127), 0);
		held.addEvent(juce::MidiMessage::noteOn(1, 48, 0.8f), 0);
		held.addEvent(juce::MidiMessage::noteOff(1, 48), 64);
		renderBlock(coupled, actual, held);
		renderBlock(legacy, baseline, held);
		for (auto* processor : { &coupled, &legacy })
			setParameter(*processor, vekt::mono::parameters::quality, static_cast<float>(target));
		renderBlock(coupled, actual);
		renderBlock(legacy, baseline);
		REQUIRE(coupled.hasPendingQualityChange());
		REQUIRE(coupled.getActiveQuality() == initial);
		REQUIRE(coupled.getLatencySamples() == previousLatency);
		playHead.playing = false;
		renderBlock(coupled, actual);
		renderBlock(legacy, baseline);
		REQUIRE(coupled.hasPendingQualityChange());
		juce::MidiBuffer releaseSustain;
		releaseSustain.addEvent(juce::MidiMessage::controllerEvent(1, 64, 0), 0);
		renderBlock(coupled, actual, releaseSustain);
		renderBlock(legacy, baseline, releaseSustain);
		REQUIRE(coupled.hasPendingQualityChange());
		for (int block = 0; block < 32 && coupled.hasPendingQualityChange(); ++block)
		{
			renderBlock(coupled, actual);
			renderBlock(legacy, baseline);
		}
		REQUIRE_FALSE(coupled.hasPendingQualityChange());
		REQUIRE_FALSE(legacy.hasPendingQualityChange());
		REQUIRE(coupled.getActiveQuality() == target);
		REQUIRE(legacy.getActiveQuality() == target);
		REQUIRE(coupled.getLatencySamples() == legacy.getLatencySamples());
		REQUIRE(coupled.isCoupledLadderActive());
		REQUIRE_FALSE(legacy.isDevelopmentLadderActive());
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
#endif

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
	REQUIRE(emphasizedRms >= flatRms);
	REQUIRE(emphasizedPeak > flatPeak * 1.5f);
}

TEST_CASE("Mono uncompensated Ladder loses passband level with emphasis", "[mono][processor][filter][qcomp]")
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

TEST_CASE("Mono Ladder self-oscillates at maximum emphasis", "[mono][processor][filter]")
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
			setParameter(processor, vekt::mono::parameters::noiseType, 0.0f);
			setParameter(processor, vekt::mono::parameters::noiseLevel, 0.0f);
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
			for (int block = 0; block < 24; ++block) renderBlock(processor, buffer);
			const auto settledRms = rms(buffer);
			const auto [frequency, magnitude] = dominantFrequency(buffer, cutoff * 0.9f, cutoff * 1.1f, sampleRate);
			const auto secondHarmonic = sinusoidMagnitude(buffer, frequency * 2.0f, sampleRate);
			const auto thirdHarmonic = sinusoidMagnitude(buffer, frequency * 3.0f, sampleRate);
			INFO("quality=" << quality << ", sample rate=" << sampleRate << ", cutoff=" << cutoff << ", fundamental=" << frequency
				<< " Hz / " << magnitude << ", second=" << secondHarmonic << ", third=" << thirdHarmonic
				<< ", rms=" << settledRms);
			REQUIRE(settledRms > 0.1f);
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
	setParameter(processor, vekt::mono::parameters::noiseType, 0.0f);
	setParameter(processor, vekt::mono::parameters::noiseLevel, 0.0f);
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
	for (int block = 0; block < 12; ++block) renderBlock(processor, buffer);
	REQUIRE(stereoRms(buffer) > 0.025f);
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
	REQUIRE(stereoRms(buffer) > 0.1f);
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
	REQUIRE(highResonanceCutoffTone > 0.1f);
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
	REQUIRE(drivenRms < cleanRms * 2.0f);
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

TEST_CASE("Mono provides 25 categorized factory presets", "[mono][processor]")
{
	vekt::mono::PluginProcessor processor;
	auto& session = processor.getPresetSession();
	const auto& catalog = session.library();
	REQUIRE(catalog.factoryPresetCount() == 25);
	REQUIRE(catalog.folders(vekt::presets::PresetOrigin::factory).size() == 6);
	REQUIRE(processor.getNumPrograms() == 25);
	processor.setCurrentProgram(23);
	REQUIRE(processor.getCurrentProgram() == 23);
	REQUIRE(processor.getProgramName(23) == "Transmission FX");
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
		REQUIRE(preset.soundSchemaVersion == 4);
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

TEST_CASE("Mono voice stealing avoids an exceptional sample-boundary jump", "[mono][processor][midi][declick]")
{
	vekt::mono::PluginProcessor processor;
	setParameter(processor, vekt::mono::parameters::performanceMode, 0.0f);
	setParameter(processor, vekt::mono::parameters::voiceCount, 0.0f);
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
