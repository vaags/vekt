#include <vekt/mono/PluginProcessor.h>

#include "MonoVoice.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <utility>

namespace
{
namespace parameters = vekt::mono::parameters;

void setParameter(vekt::mono::PluginProcessor& processor, const char* identifier, float value)
{
	auto* parameter = processor.getParameters().getParameter(identifier);
	parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

// One saw through the open ladder, unison layers centred (no spread) and no noise.
vekt::mono::MonoVoiceSettings sawSettings(int unison, float detune)
{
	vekt::mono::MonoVoiceSettings settings {};
	settings.rangeOctaves.fill(0); // 8'
	settings.level = { 0.5f, 0.0f, 0.0f };
	settings.morph.fill(2.0f);
	settings.pulseWidth.fill(50.0f);
	settings.cutoff = 20'000.0f;
	settings.ampAttack = 0.0005f;
	settings.ampSustain = 1.0f;
	settings.ampRelease = 0.3f;
	settings.filterRelease = 0.3f;
	settings.unison = unison;
	settings.detune = detune;
	return settings;
}

// Mean power over many fresh notes, each with its own unison phase draw.
double meanPower(int unison, float detune)
{
	vekt::mono::MonoVoice voice;
	voice.prepare(48'000.0, 0x4d6f6e6fu);
	const auto settings = sawSettings(unison, detune);
	double sum {};
	int count {};
	for (int note = 0; note < 24; ++note)
	{
		voice.stop();
		voice.start(1, 57, 0.8f, settings, true, false, static_cast<std::uint64_t>(note + 1));
		for (int sample = 0; sample < 24'000; ++sample)
		{
			float left {}, right {};
			voice.render(left, right, settings, 0.0f);
			if (sample >= 2'400)
			{
				sum += static_cast<double>(left) * left;
				++count;
			}
		}
	}
	return sum / count;
}
}

TEST_CASE("Mono unison gain runs from 1/N for identical layers to 1/sqrt N for decorrelated ones", "[mono][unison]")
{
	REQUIRE(vekt::mono::unisonSpreadFactor(0.0f) == 0.0f);
	REQUIRE(vekt::mono::unisonSpreadFactor(2.5f) == Catch::Approx(0.5f));
	REQUIRE(vekt::mono::unisonSpreadFactor(5.0f) == 1.0f);
	REQUIRE(vekt::mono::unisonSpreadFactor(50.0f) == 1.0f);
	REQUIRE(vekt::mono::unisonGain(1, 1.0f) == 1.0f);
	REQUIRE(vekt::mono::unisonGain(2, 0.0f) == Catch::Approx(0.5f));
	REQUIRE(vekt::mono::unisonGain(4, 0.0f) == Catch::Approx(0.25f));
	REQUIRE(vekt::mono::unisonGain(2, 1.0f) == Catch::Approx(1.0f / std::sqrt(2.0f)));
	REQUIRE(vekt::mono::unisonGain(4, 1.0f) == Catch::Approx(0.5f));
}

TEST_CASE("Mono unison keeps the 1x level on average at the default detune", "[mono][unison][slow]")
{
	const auto single = meanPower(1, 15.0f);
	for (const auto unison : { 2, 4 })
	{
		CAPTURE(unison);
		REQUIRE(10.0 * std::log10(meanPower(unison, 15.0f) / single) == Catch::Approx(0.0).margin(1.0));
	}
}

TEST_CASE("Mono unison at zero detune sounds exactly like 1x", "[mono][unison]")
{
	const auto render = [](float unison)
	{
		vekt::mono::PluginProcessor processor;
		for (auto* parameter : processor.juce::AudioProcessor::getParameters())
			parameter->setValueNotifyingHost(parameter->getDefaultValue());
		setParameter(processor, parameters::performanceMode, 0.0f);
		setParameter(processor, parameters::unison, unison);
		setParameter(processor, parameters::unisonDetune, 0.0f);
		processor.prepareToPlay(48'000.0, 4'800);
		juce::AudioBuffer<float> buffer(2, 4'800);
		juce::MidiBuffer chord;
		// Several notes land on different voice slots; none may comb-filter or change level.
		for (const auto note : { 45, 52, 57, 64 }) chord.addEvent(juce::MidiMessage::noteOn(1, note, 0.8f), 0);
		processor.processBlock(buffer, chord);
		return buffer;
	};
	const auto single = render(0.0f);
	REQUIRE(single.getRMSLevel(0, 0, 4'800) > 0.01f);
	for (const auto unison : { 1.0f, 2.0f })
	{
		CAPTURE(unison);
		const auto layered = render(unison);
		for (int channel = 0; channel < 2; ++channel)
			for (int sample = 0; sample < 4'800; ++sample)
				REQUIRE(layered.getSample(channel, sample) == Catch::Approx(single.getSample(channel, sample)).margin(1.0e-6));
	}
}

TEST_CASE("Mono Unison Detune defaults to 15 cents", "[mono][unison][parameters]")
{
	vekt::mono::PluginProcessor processor;
	auto* detune = dynamic_cast<juce::RangedAudioParameter*>(processor.getParameters().getParameter(parameters::unisonDetune));
	REQUIRE(detune->convertFrom0to1(detune->getDefaultValue()) == Catch::Approx(15.0f));
	REQUIRE(detune->getLabel() == "ct");
}

namespace
{
// A held note in 512-sample blocks; `between` runs once, after `changeAtBlock` blocks.
template <typename Change>
juce::AudioBuffer<float> holdNote(vekt::mono::PluginProcessor& processor, int blocks, int changeAtBlock, Change between)
{
	processor.prepareToPlay(48'000.0, 512);
	juce::AudioBuffer<float> output(2, blocks * 512), block(2, 512);
	for (int index = 0; index < blocks; ++index)
	{
		if (index == changeAtBlock) between();
		juce::MidiBuffer midi;
		if (index == 0) midi.addEvent(juce::MidiMessage::noteOn(1, 57, 0.8f), 0);
		block.clear();
		processor.processBlock(block, midi);
		for (int channel = 0; channel < 2; ++channel) output.copyFrom(channel, index * 512, block, channel, 0, 512);
	}
	return output;
}

void initializeUnisonSaw(vekt::mono::PluginProcessor& processor, float unison, float detune)
{
	for (auto* parameter : processor.juce::AudioProcessor::getParameters())
		parameter->setValueNotifyingHost(parameter->getDefaultValue());
	setParameter(processor, parameters::osc1Morph, 2.0f);
	setParameter(processor, parameters::filterCutoff, 20'000.0f);
	setParameter(processor, parameters::filterEnvelopeAmount, 0.0f);
	setParameter(processor, parameters::ampSustain, 100.0f);
	setParameter(processor, parameters::unison, unison);
	setParameter(processor, parameters::unisonDetune, detune);
}

float decibels(float ratio) { return 20.0f * std::log10(ratio); }
}

TEST_CASE("Mono unison noise correlation keeps the noise at the 1x level", "[mono][unison]")
{
	REQUIRE(vekt::mono::unisonNoiseCorrelation(1, 0.5f) == 1.0f);
	for (const auto layers : { 2, 4 })
	{
		CAPTURE(layers);
		REQUIRE(vekt::mono::unisonNoiseCorrelation(layers, 0.0f) == Catch::Approx(1.0f));
		REQUIRE(vekt::mono::unisonNoiseCorrelation(layers, 1.0f) == Catch::Approx(0.0f).margin(1.0e-6));
		// Summed power of N layers with this correlation, times the unison gain squared, is exactly one layer's.
		for (const auto spread : { 0.0f, 0.3f, 0.7f, 1.0f })
		{
			const auto rho = vekt::mono::unisonNoiseCorrelation(layers, spread);
			const auto gain = vekt::mono::unisonGain(layers, spread);
			const auto count = static_cast<float>(layers);
			REQUIRE(gain * gain * (count + count * (count - 1.0f) * rho) == Catch::Approx(1.0f));
		}
	}
}

TEST_CASE("Mono unison detune automation on a held note does not jump in level", "[mono][unison]")
{
	// 0 to 5 cents used to switch 4x from 1/4 to 1/2 gain at once while the layers were still in phase (+6 dB).
	for (const auto [from, to] : { std::pair { 0.0f, 5.0f }, std::pair { 0.0f, 50.0f }, std::pair { 15.0f, 0.0f } })
	{
		CAPTURE(from, to);
		vekt::mono::PluginProcessor processor;
		initializeUnisonSaw(processor, 2.0f, from);
		const auto output = holdNote(processor, 20, 10, [&] { setParameter(processor, parameters::unisonDetune, to); });
		const auto before = output.getRMSLevel(0, 9 * 512, 512);
		const auto after = output.getRMSLevel(0, 10 * 512, 512);
		REQUIRE(std::abs(decibels(after / before)) < 1.0f);
	}
}

TEST_CASE("Mono unison at zero detune matches 1x with white and pink noise", "[mono][unison]")
{
	for (const auto noiseType : { 1.0f, 2.0f })
	{
		CAPTURE(noiseType);
		const auto render = [noiseType](float unison)
		{
			vekt::mono::PluginProcessor processor;
			initializeUnisonSaw(processor, unison, 0.0f);
			setParameter(processor, parameters::noiseType, noiseType);
			setParameter(processor, parameters::noiseLevel, 60.0f);
			return holdNote(processor, 8, -1, [] {});
		};
		const auto single = render(0.0f);
		for (const auto unison : { 1.0f, 2.0f })
		{
			CAPTURE(unison);
			const auto layered = render(unison);
			for (int channel = 0; channel < 2; ++channel)
				for (int sample = 0; sample < single.getNumSamples(); ++sample)
					REQUIRE(layered.getSample(channel, sample) == Catch::Approx(single.getSample(channel, sample)).margin(1.0e-6));
		}
	}
}

TEST_CASE("Mono unison keeps noise at the 1x level at the default detune", "[mono][unison]")
{
	for (const auto noiseType : { 1.0f, 2.0f })
	{
		CAPTURE(noiseType);
		const auto noiseRms = [noiseType](float unison)
		{
			vekt::mono::PluginProcessor processor;
			initializeUnisonSaw(processor, unison, 15.0f);
			setParameter(processor, parameters::osc1Level, 0.0f);
			setParameter(processor, parameters::noiseType, noiseType);
			setParameter(processor, parameters::noiseLevel, 60.0f);
			const auto output = holdNote(processor, 48, -1, [] {});
			return output.getRMSLevel(0, 2'048, output.getNumSamples() - 2'048);
		};
		const auto single = noiseRms(0.0f);
		for (const auto unison : { 1.0f, 2.0f })
		{
			CAPTURE(unison);
			REQUIRE(std::abs(decibels(noiseRms(unison) / single)) < 0.5f);
		}
	}
}
