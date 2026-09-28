#include <vekt/mono/PluginProcessor.h>

#include "MonoVoice.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>

namespace
{
using vekt::mono::MonoVoice;

constexpr int cycleSamples = 4'096;
constexpr float phaseIncrement = 1.0f / static_cast<float>(cycleSamples);
constexpr float targetRms = 0.57735027f; // the saw's RMS, 1/sqrt 3

template <typename Wave>
float cycleRms(Wave wave)
{
	double sum {};
	for (int sample = 0; sample < cycleSamples; ++sample)
	{
		const auto value = static_cast<double>(wave(static_cast<float>(sample) * phaseIncrement));
		sum += value * value;
	}
	return static_cast<float>(std::sqrt(sum / cycleSamples));
}

// Sine and cosine parts of the fundamental over one cycle.
template <typename Wave>
std::pair<float, float> fundamental(Wave wave)
{
	double sine {}, cosine {};
	for (int sample = 0; sample < cycleSamples; ++sample)
	{
		const auto phase = static_cast<double>(sample) / cycleSamples;
		const auto value = static_cast<double>(wave(static_cast<float>(phase)));
		sine += value * std::sin(2.0 * std::numbers::pi * phase);
		cosine += value * std::cos(2.0 * std::numbers::pi * phase);
	}
	return { static_cast<float>(2.0 * sine / cycleSamples), static_cast<float>(2.0 * cosine / cycleSamples) };
}

auto atMorph(float morph, float width = 50.0f)
{
	return [morph, width](float phase) { return MonoVoice::waveform(phase, phaseIncrement, morph, width); };
}

float decibels(float ratio) { return 20.0f * std::log10(ratio); }
}

TEST_CASE("Mono oscillator anchors share the fundamental's phase and sign", "[mono][oscillator]")
{
	for (int anchor = 0; anchor <= 3; ++anchor)
	{
		CAPTURE(anchor);
		const auto [sine, cosine] = fundamental(atMorph(static_cast<float>(anchor)));
		REQUIRE(sine > 0.3f);
		REQUIRE(std::abs(cosine) < 0.01f * sine);
	}
}

TEST_CASE("Mono oscillator anchors have matching RMS at the saw's level", "[mono][oscillator]")
{
	for (int anchor = 0; anchor <= 3; ++anchor)
	{
		CAPTURE(anchor);
		REQUIRE(decibels(cycleRms(atMorph(static_cast<float>(anchor))) / targetRms) == Catch::Approx(0.0f).margin(0.05f));
	}
	// A +/-1 pulse keeps the same RMS at every width, so one gain matches the pulse anchor across Width.
	for (const auto width : { 5.0f, 20.0f, 80.0f, 95.0f })
	{
		CAPTURE(width);
		REQUIRE(decibels(cycleRms(atMorph(3.0f, width)) / targetRms) == Catch::Approx(0.0f).margin(0.1f));
	}
}

TEST_CASE("Mono oscillator level stays within 1 dB across the whole Morph range", "[mono][oscillator]")
{
	for (int step = 0; step <= 60; ++step)
	{
		const auto morph = static_cast<float>(step) * 0.05f;
		CAPTURE(morph);
		REQUIRE(std::abs(decibels(cycleRms(atMorph(morph)) / targetRms)) < 1.0f);
	}
}

TEST_CASE("Mono oscillator anchors are the canonical shapes", "[mono][oscillator]")
{
	const auto wave = [](float morph, float phase) { return MonoVoice::waveform(phase, phaseIncrement, morph, 50.0f); };
	// Sine and triangle peak at a quarter cycle; the saw falls through zero at half a cycle; the square is high first.
	REQUIRE(wave(0.0f, 0.25f) == Catch::Approx(MonoVoice::sineAnchorGain));
	REQUIRE(wave(1.0f, 0.25f) == Catch::Approx(1.0f));
	REQUIRE(wave(1.0f, 0.75f) == Catch::Approx(-1.0f));
	REQUIRE(wave(1.0f, 0.0f) == Catch::Approx(0.0f).margin(1.0e-6));
	REQUIRE(wave(2.0f, 0.25f) == Catch::Approx(0.5f));
	REQUIRE(wave(2.0f, 0.5f) == Catch::Approx(0.0f).margin(1.0e-6));
	REQUIRE(wave(2.0f, 0.75f) == Catch::Approx(-0.5f));
	REQUIRE(wave(3.0f, 0.25f) == Catch::Approx(MonoVoice::pulseAnchorGain));
	REQUIRE(wave(3.0f, 0.75f) == Catch::Approx(-MonoVoice::pulseAnchorGain));
	// Every anchor position returns exactly that anchor, and positions between them interpolate linearly.
	for (int sample = 0; sample < 64; ++sample)
	{
		const auto phase = static_cast<float>(sample) / 64.0f;
		for (int anchor = 0; anchor <= 3; ++anchor)
			REQUIRE(wave(static_cast<float>(anchor), phase) == MonoVoice::anchorWave(anchor, phase, phaseIncrement, 50.0f));
		const auto from = MonoVoice::anchorWave(1, phase, phaseIncrement, 50.0f);
		const auto to = MonoVoice::anchorWave(2, phase, phaseIncrement, 50.0f);
		REQUIRE(wave(1.3f, phase) == Catch::Approx(from + 0.3f * (to - from)).margin(1.0e-6));
	}
}

TEST_CASE("Mono oscillator morph is continuous across the triangle and saw anchors", "[mono][oscillator]")
{
	for (const auto anchor : { 1.0f, 2.0f })
	{
		CAPTURE(anchor);
		float largestJump {};
		for (int sample = 0; sample < cycleSamples; ++sample)
		{
			const auto phase = static_cast<float>(sample) * phaseIncrement;
			const auto below = MonoVoice::waveform(phase, phaseIncrement, std::nextafter(anchor, 0.0f), 50.0f);
			const auto above = MonoVoice::waveform(phase, phaseIncrement, std::nextafter(anchor, 3.0f), 50.0f);
			largestJump = std::max(largestJump, std::abs(above - below));
		}
		REQUIRE(largestJump < 1.0e-5f);
	}
}

TEST_CASE("Mono voice output level stays within 1 dB across Morph through the open ladder", "[mono][oscillator]")
{
	const auto outputRms = [](float morph)
	{
		vekt::mono::PluginProcessor processor;
		for (auto* parameter : processor.juce::AudioProcessor::getParameters())
			parameter->setValueNotifyingHost(parameter->getDefaultValue());
		const auto set = [&processor](const char* identifier, float value)
		{
			auto* parameter = processor.getParameters().getParameter(identifier);
			parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
		};
		set(vekt::mono::parameters::osc1Morph, morph);
		set(vekt::mono::parameters::osc1Level, 50.0f);
		set(vekt::mono::parameters::filterCutoff, 20'000.0f);
		set(vekt::mono::parameters::filterResonance, 0.0f);
		set(vekt::mono::parameters::filterEnvelopeAmount, 0.0f);
		set(vekt::mono::parameters::ampSustain, 100.0f);
		processor.prepareToPlay(48'000.0, 4'800);
		juce::AudioBuffer<float> buffer(2, 4'800);
		juce::MidiBuffer note;
		note.addEvent(juce::MidiMessage::noteOn(1, 45, 0.8f), 0);
		processor.processBlock(buffer, note);
		juce::MidiBuffer none;
		buffer.clear();
		processor.processBlock(buffer, none);
		return buffer.getRMSLevel(0, 0, buffer.getNumSamples());
	};
	const auto saw = outputRms(2.0f);
	for (int step = 0; step <= 12; ++step)
	{
		const auto morph = static_cast<float>(step) * 0.25f;
		CAPTURE(morph);
		REQUIRE(std::abs(decibels(outputRms(morph) / saw)) < 1.0f);
	}
}
