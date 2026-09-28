#include <vekt/mono/PluginProcessor.h>

#include "MonoVoice.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_dsp/juce_dsp.h>

#include <cmath>
#include <numbers>
#include <vector>

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
	// The triangle's corners are rounded by polyBLAMP by exactly 4 * increment / 3.
	REQUIRE(wave(1.0f, 0.25f) == Catch::Approx(1.0f - 4.0f * phaseIncrement / 3.0f));
	REQUIRE(wave(1.0f, 0.75f) == Catch::Approx(-1.0f + 4.0f * phaseIncrement / 3.0f));
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
		const auto sine = MonoVoice::anchorWave(0, phase, phaseIncrement, 50.0f);
		const auto triangle = MonoVoice::anchorWave(1, phase, phaseIncrement, 50.0f);
		const auto saw = MonoVoice::anchorWave(2, phase, phaseIncrement, 50.0f);
		const auto square = MonoVoice::anchorWave(3, phase, phaseIncrement, 50.0f);
		// Sine to triangle mixes linearly; next to the saw the saw's share is 2t^2 - t^3 (37.5% halfway).
		REQUIRE(wave(0.3f, phase) == Catch::Approx(sine + 0.3f * (triangle - sine)).margin(1.0e-6));
		REQUIRE(wave(1.5f, phase) == Catch::Approx(triangle + 0.375f * (saw - triangle)).margin(1.0e-6));
		REQUIRE(wave(2.5f, phase) == Catch::Approx(0.375f * saw + 0.625f * square).margin(1.0e-6));
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

TEST_CASE("Mono saw morph curve delays the saw but meets it at the linear rate", "[mono][oscillator]")
{
	using vekt::mono::sawMorphWeight;
	REQUIRE(sawMorphWeight(0.0f) == 0.0f);
	REQUIRE(sawMorphWeight(0.25f) == Catch::Approx(0.109375f));
	REQUIRE(sawMorphWeight(0.5f) == Catch::Approx(0.375f));
	REQUIRE(sawMorphWeight(0.75f) == Catch::Approx(0.703125f));
	REQUIRE(sawMorphWeight(1.0f) == 1.0f);
	// Arrives at the saw anchor with slope 1, like linear morphing, so LFO sweeps do not speed up there.
	constexpr float step = 1.0e-3f;
	REQUIRE((sawMorphWeight(1.0f) - sawMorphWeight(1.0f - step)) / step == Catch::Approx(1.0f).margin(0.01f));
	float previous {};
	for (int index = 1; index <= 100; ++index)
	{
		const auto weight = sawMorphWeight(static_cast<float>(index) / 100.0f);
		REQUIRE(weight > previous);
		previous = weight;
	}
}

TEST_CASE("Mono triangle polyBLAMP reduces aliasing without changing the anchor", "[mono][oscillator]")
{
	// A high triangle whose harmonics fold back below Nyquist; measure energy away from true harmonics.
	constexpr int order = 14;
	constexpr int size = 1 << order;
	constexpr double sampleRate = 48'000.0;
	constexpr double frequency = 3'517.3;
	const auto increment = static_cast<float>(frequency / sampleRate);
	const auto aliasRatioDb = [&](auto wave)
	{
		std::vector<float> data(2 * size);
		double phase {};
		for (int sample = 0; sample < size; ++sample)
		{
			const auto window = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * sample / size);
			data[static_cast<std::size_t>(sample)] = static_cast<float>(wave(static_cast<float>(phase)) * window);
			phase += frequency / sampleRate;
			phase -= std::floor(phase);
		}
		juce::dsp::FFT(order).performFrequencyOnlyForwardTransform(data.data());
		double alias {}, total {};
		for (int bin = 1; bin < size / 2; ++bin)
		{
			const auto binFrequency = bin * sampleRate / size;
			const auto harmonic = std::round(binFrequency / frequency);
			const auto nearHarmonic = harmonic >= 1.0 && std::abs(binFrequency - harmonic * frequency) < 4.0 * sampleRate / size;
			const auto power = static_cast<double>(data[static_cast<std::size_t>(bin)]) * data[static_cast<std::size_t>(bin)];
			total += power;
			if (!nearHarmonic) alias += power;
		}
		return 10.0 * std::log10(alias / total);
	};
	const auto naive = aliasRatioDb([](float phase)
	{
		auto shifted = phase + 0.25f;
		shifted -= std::floor(shifted);
		return 1.0f - 4.0f * std::abs(shifted - 0.5f);
	});
	const auto blamp = aliasRatioDb([increment](float phase) { return MonoVoice::anchorWave(1, phase, increment, 50.0f); });
	CAPTURE(naive, blamp);
	REQUIRE(blamp < naive - 6.0);
	// Away from its corners the anchor is still the exact triangle.
	REQUIRE(MonoVoice::anchorWave(1, 0.1f, increment, 50.0f) == Catch::Approx(0.4f));
	REQUIRE(MonoVoice::anchorWave(1, 0.6f, increment, 50.0f) == Catch::Approx(-0.4f));
}

TEST_CASE("Mono Morph knob changes are smoothed but LFO morph modulation is not", "[mono][oscillator][lfo]")
{
	constexpr double sampleRate = 48'000.0;
	vekt::mono::MonoVoiceSettings settings {}; // value-initialised: the oscillator arrays have no default member initialisers
	settings.range.fill(1.0f);
	settings.level = { 1.0f, 0.0f, 0.0f };
	settings.morph.fill(0.0f);
	settings.pulseWidth.fill(50.0f);
	settings.cutoff = 20'000.0f;
	settings.ampSustain = 1.0f;
	settings.ampRelease = 0.3f;
	settings.filterRelease = 0.3f;
	settings.unison = 1;
	vekt::mono::MonoVoice voice;
	voice.prepare(sampleRate, 7);
	voice.start(1, 57, 0.8f, settings, true, false, 1);
	float left {}, right {};
	for (int sample = 0; sample < 480; ++sample) voice.render(left, right, settings, 0.0f);
	REQUIRE(voice.getMorph(0) == 0.0f);

	// A knob jump from sine to square ramps linearly over 10 ms (480 samples).
	settings.morph[0] = 3.0f;
	voice.render(left, right, settings, 0.0f);
	REQUIRE(voice.getMorph(0) < 0.05f);
	for (int sample = 1; sample < 240; ++sample) voice.render(left, right, settings, 0.0f);
	REQUIRE(voice.getMorph(0) == Catch::Approx(1.5f).margin(0.02f));
	for (int sample = 240; sample < 480; ++sample) voice.render(left, right, settings, 0.0f);
	REQUIRE(voice.getMorph(0) == Catch::Approx(3.0f));

	// A new note on a silent voice starts at the knob's value, not partway through a ramp.
	vekt::mono::MonoVoice fresh;
	fresh.prepare(sampleRate, 7);
	fresh.start(1, 57, 0.8f, settings, true, false, 1);
	fresh.render(left, right, settings, 0.0f);
	REQUIRE(fresh.getMorph(0) == 3.0f);

	// LFO modulation reaches the oscillator without the 10 ms ramp: only the LFO's own ~1 ms de-click.
	settings.morph[0] = 0.0f;
	settings.lfo[0].source = { .rateHz = 0.01f, .shape = vekt::mono::LfoShape::square,
		.polarity = vekt::mono::LfoPolarity::unipolar, .mode = vekt::mono::LfoMode::retrigger };
	settings.lfo[0].morph[0] = 3.0f;
	vekt::mono::MonoVoice modulated;
	modulated.prepare(sampleRate, 7);
	modulated.start(1, 57, 0.8f, settings, true, false, 1);
	for (int sample = 0; sample < 240; ++sample) modulated.render(left, right, settings, 0.0f);
	// After 5 ms the LFO offset is essentially complete; a smoothed knob would still be at half its travel.
	REQUIRE(modulated.getMorph(0) > 2.95f);
}
