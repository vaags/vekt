#include <vekt/mono/PluginProcessor.h>

#include "MonoVoice.h"
#include <audio_lab/MonoWidthReference.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_dsp/juce_dsp.h>

#include <cmath>
#include <complex>
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

// Mono's Width oscillator at 25 Hz / 48 kHz: every guard-passing harmonic (up to H864) is present, and a
// 4096-sample cycle resolves them all without aliasing.
constexpr double oscillatorRate = 48'000.0;
constexpr float oscillatorPitch = 25.0f;

float oscillator(float phase, float morph, float width = 50.0f, bool zeroCentered = false)
{
	static vekt::mono::WidthOscillatorState state;
	return vekt::mono::renderWidthOscillator(state, phase, oscillatorPitch, oscillatorRate, morph, width, zeroCentered);
}

auto atMorph(float morph, float width = 50.0f)
{
	return [morph, width](float phase) { return oscillator(phase, morph, width); };
}

float decibels(float ratio) { return 20.0f * std::log10(ratio); }
}

TEST_CASE("Mono Width offline Fourier reference preserves ideal phase and frozen DC", "[mono][oscillator][width][reference]")
{
	using vekt::audio_lab::renderMonoWidthReference;
	// A low fundamental leaves the first 40 harmonics below Nyquist; compare
	// the inverse Fourier series with the independent ideal function away from
	// discontinuities, and check the complex fundamental's phase convention.
	const auto sine = renderMonoWidthReference(0.0, 50.0, 100.0, 48'000.0, 480, 16);
	REQUIRE(sine.harmonics[0].real() == Catch::Approx(0.0).margin(1.0e-5));
	REQUIRE(sine.harmonics[1].real() == Catch::Approx(0.0).margin(1.0e-5));
	REQUIRE(sine.harmonics[1].imag() == Catch::Approx(-static_cast<float>(vekt::mono::widthSineGain) * 0.5f).margin(1.0e-5));
	for (const auto phase : { 0.125, 0.25, 0.5, 0.875 })
		REQUIRE(sine.samples[static_cast<std::size_t>(phase * 480)]
			== Catch::Approx(vekt::audio_lab::monoWidthIdealWave(phase, 0.0, 50.0)).margin(1.0e-4));
	for (const auto width : { 5.0, 20.0, 50.0, 80.0, 95.0 })
	{
		CAPTURE(width);
		const auto raw = renderMonoWidthReference(3.0, width, 100.0, 48'000.0, 480, 16);
		const auto centered = renderMonoWidthReference(3.0, width, 100.0, 48'000.0, 480, 16, true);
		const auto expected = static_cast<float>(vekt::mono::widthPulseGain) * (width / 50.0 - 1.0);
		REQUIRE(raw.harmonics[0].real() == Catch::Approx(expected).margin(1.0e-4));
		REQUIRE(centered.harmonics[0].real() == Catch::Approx(0.0).margin(1.0e-4));
		REQUIRE(std::abs(centered.harmonics[1] - raw.harmonics[1]) < 1.0e-5);
		// Round-trip one cycle: the DC term is the sample mean, and the Fourier
		// coefficient c[1] encodes the fundamental magnitude and phase.
		double average {};
		for (const auto value : raw.samples) average += value;
		REQUIRE(average / static_cast<double>(raw.samples.size()) == Catch::Approx(expected).margin(1.0e-4));
	}
	// Verify the midpoint quadrature/FFT resolution does not set the first
	// harmonics in the most discontinuous supported waveform.
	const auto coarse = renderMonoWidthReference(3.0, 5.0, 3'517.3, 48'000.0, 32, 16);
	const auto fine = renderMonoWidthReference(3.0, 5.0, 3'517.3, 48'000.0, 32, 17);
	REQUIRE(coarse.harmonics.size() == fine.harmonics.size());
	for (std::size_t harmonic = 0; harmonic < coarse.harmonics.size(); ++harmonic)
		REQUIRE(std::abs(coarse.harmonics[harmonic] - fine.harmonics[harmonic]) < 1.0e-4);
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
	for (int step = 0; step <= 80; ++step)
	{
		const auto morph = static_cast<float>(step) * 0.05f;
		CAPTURE(morph);
		REQUIRE(std::abs(decibels(cycleRms(atMorph(morph)) / targetRms)) < 1.0f);
	}
}

TEST_CASE("Mono oscillator anchors are the canonical shapes", "[mono][oscillator]")
{
	// Bandlimited anchors: away from edges and corners they sit within the truncation ripple of the ideal shapes.
	const auto wave = [](float morph, float phase) { return oscillator(phase, morph); };
	const auto sineGain = static_cast<float>(vekt::mono::widthSineGain), pulseGain = static_cast<float>(vekt::mono::widthPulseGain);
	// Sine and triangle peak at a quarter cycle; the saw falls through zero at half a cycle; the square is high first.
	REQUIRE(wave(0.0f, 0.25f) == Catch::Approx(sineGain).margin(1.0e-4));
	REQUIRE(wave(1.0f, 0.25f) == Catch::Approx(1.0f).margin(2.0e-3));
	REQUIRE(wave(1.0f, 0.75f) == Catch::Approx(-1.0f).margin(2.0e-3));
	REQUIRE(wave(1.0f, 0.0f) == Catch::Approx(0.0f).margin(1.0e-4));
	REQUIRE(wave(2.0f, 0.25f) == Catch::Approx(0.5f).margin(2.0e-3));
	REQUIRE(wave(2.0f, 0.5f) == Catch::Approx(0.0f).margin(1.0e-4));
	REQUIRE(wave(2.0f, 0.75f) == Catch::Approx(-0.5f).margin(2.0e-3));
	REQUIRE(wave(3.0f, 0.25f) == Catch::Approx(pulseGain).margin(2.0e-3));
	REQUIRE(wave(3.0f, 0.75f) == Catch::Approx(-pulseGain).margin(2.0e-3));
	// Positions between anchors mix them: linearly from sine to triangle; next to the saw its share is
	// 2t^2 - t^3 (37.5% halfway).
	for (int sample = 0; sample < 64; ++sample)
	{
		const auto phase = static_cast<float>(sample) / 64.0f;
		const auto sine = wave(0.0f, phase), triangle = wave(1.0f, phase), saw = wave(2.0f, phase), square = wave(3.0f, phase);
		REQUIRE(wave(0.3f, phase) == Catch::Approx(sine + 0.3f * (triangle - sine)).margin(1.0e-5));
		REQUIRE(wave(1.5f, phase) == Catch::Approx(triangle + 0.375f * (saw - triangle)).margin(1.0e-5));
		REQUIRE(wave(2.5f, phase) == Catch::Approx(0.375f * saw + 0.625f * square).margin(1.0e-5));
		// From the square back to the sine the square's share is delayed the same way (37.5% halfway at p = 2),
		// and Morph wraps: 4 is the sine again and -0.5 is 3.5.
		REQUIRE(wave(3.5f, phase) == Catch::Approx(0.375f * square + 0.625f * sine).margin(1.0e-5));
		REQUIRE(wave(4.0f, phase) == Catch::Approx(sine).margin(1.0e-6));
		REQUIRE(wave(-0.5f, phase) == Catch::Approx(wave(3.5f, phase)).margin(1.0e-6));
	}
}
TEST_CASE("Mono oscillator morph is continuous across every anchor, including the wrap to sine", "[mono][oscillator]")
{
	for (const auto anchor : { 1.0f, 2.0f, 3.0f, 4.0f })
	{
		CAPTURE(anchor);
		float largestJump {};
		for (int sample = 0; sample < cycleSamples; ++sample)
		{
			const auto phase = static_cast<float>(sample) * phaseIncrement;
			const auto below = oscillator(phase, std::nextafter(anchor, 0.0f));
			const auto above = oscillator(phase, std::nextafter(anchor, 5.0f));
			largestJump = std::max(largestJump, std::abs(above - below));
		}
		REQUIRE(largestJump < 1.0e-5f);
	}
}

TEST_CASE("Mono Width DC policies follow the frozen-width analytical means", "[mono][oscillator][width]")
{
	constexpr int samples = 32'768;
	constexpr float step = 1.0f / samples;
	for (const auto width : { 5.0f, 20.0f, 50.0f, 80.0f, 95.0f })
		for (int morphStep = 0; morphStep <= 16; ++morphStep)
		{
			const auto morph = static_cast<float>(morphStep) * 0.25f;
			CAPTURE(width, morph);
			double raw {}, centered {}, signal {};
			for (int sample = 0; sample < samples; ++sample)
			{
				const auto phase = static_cast<float>(sample) * step;
				const auto a = oscillator(phase, morph, width);
				const auto b = oscillator(phase, morph, width, true);
				raw += a; centered += b; signal += b * b;
			}
			REQUIRE(std::abs(centered / samples) < 1.0e-3);
			REQUIRE(std::isfinite(signal));
			if (morph == 3.0f)
				REQUIRE(raw / samples == Catch::Approx(static_cast<float>(vekt::mono::widthPulseGain) * (width * 0.02f - 1.0f)).margin(1.0e-3));
		}
}

TEST_CASE("Mono Width and Morph surface has finite fundamentals and continuous anchors", "[mono][oscillator][width]")
{
	constexpr int samples = 4'096;
	constexpr float step = 1.0f / samples;
	for (const auto width : { 5.0f, 10.0f, 20.0f, 30.0f, 40.0f, 50.0f, 60.0f, 70.0f, 80.0f, 90.0f, 95.0f })
	{
		for (int index = 0; index <= 80; ++index)
		{
			const auto morph = static_cast<float>(index) * 0.05f;
			CAPTURE(width, morph);
			double sine {}, cosine {}, power {};
			for (int sample = 0; sample < samples; ++sample)
			{
				const auto phase = static_cast<float>(sample) * step;
				const auto value = oscillator(phase, morph, width, true);
				sine += value * std::sin(2.0 * std::numbers::pi * phase);
				cosine += value * std::cos(2.0 * std::numbers::pi * phase);
				power += value * value;
			}
			const auto magnitude = std::hypot(sine, cosine) * 2.0 / samples;
			REQUIRE(std::isfinite(power));
			REQUIRE(magnitude > 0.05);
			for (const auto anchor : { 1.0f, 2.0f, 3.0f, 4.0f })
			{
				const auto phase = 0.123f;
				REQUIRE(std::abs(oscillator(phase, std::nextafter(anchor, 0.0f), width)
					- oscillator(phase, std::nextafter(anchor, 5.0f), width)) < 1.0e-5f);
			}
		}
	}
}

TEST_CASE("Mono Width oscillator stays alias-free on a high note", "[mono][oscillator][width]")
{
	// Coherent measurement: the pitch sits exactly on FFT bin 1200 (3515.6 Hz at 48 kHz) and no window is
	// used, so every harmonic lands on a bin and only aliases fall between them.
	constexpr int order = 14;
	constexpr int size = 1 << order;
	constexpr double rate = 48'000.0;
	constexpr int pitchBin = 1'200;
	constexpr double frequency = pitchBin * rate / size;
	const auto aliasRatio = [&](auto wave)
	{
		std::vector<float> data(2 * size);
		for (int sample = 0; sample < size; ++sample)
			data[static_cast<std::size_t>(sample)] = static_cast<float>(wave(static_cast<float>(std::fmod(sample * frequency / rate, 1.0))));
		juce::dsp::FFT(order).performFrequencyOnlyForwardTransform(data.data());
		double alias {}, total {};
		for (int bin = 1; bin < size / 2; ++bin)
		{
			const auto power = static_cast<double>(data[static_cast<std::size_t>(bin)]) * data[static_cast<std::size_t>(bin)];
			total += power;
			if (bin % pitchBin != 0) alias += power;
		}
		return 10.0 * std::log10(std::max(alias, 1.0e-30) / total);
	};
	for (const auto width : { 5.0f, 20.0f, 50.0f, 80.0f, 95.0f })
		for (int anchor = 0; anchor < 4; ++anchor)
		{
			const auto d = width * 0.01f;
			const auto naive = aliasRatio([&](float phase)
			{
				const auto warped = phase < d ? phase / (2.0f * d) : 0.5f + (phase - d) / (2.0f * (1.0f - d));
				switch (anchor)
				{
				case 0: return static_cast<float>(vekt::mono::widthSineGain) * std::sin(vekt::mono::twoPi * warped);
				case 1: return 1.0f - 4.0f * std::abs(warped + 0.25f - std::floor(warped + 0.25f) - 0.5f);
				case 2: return 1.0f - 2.0f * warped;
				default: return static_cast<float>(vekt::mono::widthPulseGain) * (warped < 0.5f ? 1.0f : -1.0f);
				}
			});
			// The naive shared warp is an aliasing yardstick only; its shape differs from the shipped anchors.
			vekt::mono::WidthOscillatorState state;
			const auto bandlimited = aliasRatio([&](float phase)
				{ return vekt::mono::renderWidthOscillator(state, phase, static_cast<float>(frequency), rate, static_cast<float>(anchor), width, false); });
			CAPTURE(width, anchor, naive, bandlimited);
			REQUIRE(bandlimited < -80.0);
		}
}

TEST_CASE("Mono Width DC policy changes the ladder input on a held narrow pulse", "[mono][oscillator][width]")
{
	vekt::mono::MonoVoiceSettings settings {};
	settings.range.fill(1.0f);
	settings.level = { 0.5f, 0.0f, 0.0f };
	settings.morph.fill(3.0f);
	settings.pulseWidth.fill(20.0f);
	settings.cutoff = 1'000.0f;
	settings.resonance = 0.85f;
	settings.ampSustain = 1.0f;
	settings.unison = 1;
	MonoVoice raw, centered;
	raw.prepare(48'000.0, 42);
	centered.prepare(48'000.0, 42);
	raw.start(1, 57, 0.8f, settings, true, false, 1);
	settings.widthDcPolicy = vekt::mono::WidthDcPolicy::zeroCentered;
	centered.start(1, 57, 0.8f, settings, true, false, 1);
	double difference {};
	for (int sample = 0; sample < 4'800; ++sample)
	{
		float rawLeft {}, rawRight {}, centeredLeft {}, centeredRight {};
		settings.widthDcPolicy = vekt::mono::WidthDcPolicy::raw;
		raw.render(rawLeft, rawRight, settings, 0.0f);
		settings.widthDcPolicy = vekt::mono::WidthDcPolicy::zeroCentered;
		centered.render(centeredLeft, centeredRight, settings, 0.0f);
		REQUIRE(std::isfinite(rawLeft));
		REQUIRE(std::isfinite(centeredLeft));
		difference += std::abs(rawLeft - centeredLeft);
	}
	REQUIRE(difference > 1.0);
}

TEST_CASE("Mono moving Width and Morph remain finite through the voice and ladder", "[mono][oscillator][width]")
{
	vekt::mono::MonoVoiceSettings settings {};
	settings.range.fill(1.0f);
	settings.level = { 0.5f, 0.0f, 0.0f };
	settings.pulseWidth.fill(50.0f);
	settings.cutoff = 2'000.0f;
	settings.resonance = 0.9f;
	settings.drive = 12.0f;
	settings.ampSustain = 1.0f;
	settings.unison = 1;
	MonoVoice voice;
	voice.prepare(48'000.0, 42);
	voice.start(1, 57, 0.8f, settings, true, false, 1);
	for (int sample = 0; sample < 4'800; ++sample)
	{
		// Direct voice settings exercise the audio path; these jumps deliberately
		// include an adversarial discontinuity, not a claim of click-free automation.
		settings.pulseWidth[0] = sample % 64 < 32 ? 5.0f : 95.0f;
		settings.morph[0] = sample % 800 < 400 ? 0.0f : 3.0f;
		float left {}, right {};
		voice.render(left, right, settings, 0.0f);
		REQUIRE(std::isfinite(left));
		REQUIRE(std::isfinite(right));
	}
	REQUIRE(voice.coupledDiagnostics().nonFiniteSamples == 0);
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
	for (int step = 0; step <= 16; ++step)
	{
		const auto morph = static_cast<float>(step) * 0.25f;
		CAPTURE(morph);
		REQUIRE(std::abs(decibels(outputRms(morph) / saw)) < 1.0f);
	}
}

TEST_CASE("Mono morph curves delay the richer anchor but meet it at the linear rate", "[mono][oscillator]")
{
	using vekt::mono::delayedMorphWeight;
	using vekt::mono::morphSegmentBlend;
	REQUIRE(delayedMorphWeight(0.0) == 0.0);
	REQUIRE(delayedMorphWeight(0.25) == Catch::Approx(0.109375));
	REQUIRE(delayedMorphWeight(0.5) == Catch::Approx(0.375));
	REQUIRE(delayedMorphWeight(0.75) == Catch::Approx(0.703125));
	REQUIRE(delayedMorphWeight(1.0) == 1.0);
	// Meets the rich anchor with slope 1, like linear morphing, so LFO sweeps do not speed up there.
	constexpr double step = 1.0e-4;
	REQUIRE((1.0 - delayedMorphWeight(1.0 - step)) / step == Catch::Approx(1.0).margin(0.01));
	// Sine to triangle is linear; the saw's share and the square's share are delayed: 37.5% halfway.
	REQUIRE(morphSegmentBlend(0, 0.3) == Catch::Approx(0.3));
	REQUIRE(morphSegmentBlend(1, 0.5) == Catch::Approx(0.375));
	REQUIRE(morphSegmentBlend(2, 0.5) == Catch::Approx(0.625));
	REQUIRE(morphSegmentBlend(3, 0.5) == Catch::Approx(0.625));
	for (int segment = 0; segment <= 3; ++segment)
	{
		CAPTURE(segment);
		REQUIRE(morphSegmentBlend(segment, 0.0) == 0.0);
		REQUIRE(morphSegmentBlend(segment, 1.0) == 1.0);
		double previous = -1.0;
		for (int index = 0; index <= 100; ++index)
		{
			const auto blend = morphSegmentBlend(segment, index / 100.0);
			REQUIRE(blend > previous);
			previous = blend;
		}
	}
	// The square is left at the linear rate, so sweeps do not jump away from it.
	REQUIRE(morphSegmentBlend(3, step) / step == Catch::Approx(1.0).margin(0.01));
}

TEST_CASE("Mono Morph wraps round its cycle", "[mono][oscillator]")
{
	using vekt::mono::morphDistance;
	using vekt::mono::wrapMorph;
	REQUIRE(wrapMorph(0.0f) == 0.0f);
	REQUIRE(wrapMorph(3.5f) == 3.5f);
	REQUIRE(wrapMorph(4.0f) == 0.0f);
	REQUIRE(wrapMorph(5.25f) == 1.25f);
	REQUIRE(wrapMorph(-0.5f) == 3.5f);
	REQUIRE(wrapMorph(-1.0e-9f) < 4.0f);
	// Genuinely modulo 4, several turns out, as summed modulation sources can reach.
	REQUIRE(wrapMorph(8.0f) == 0.0f);
	REQUIRE(wrapMorph(12.25f) == 0.25f);
	REQUIRE(wrapMorph(-4.0f) == 0.0f);
	REQUIRE(wrapMorph(-4.5f) == 3.5f);
	REQUIRE(wrapMorph(-8.5f) == 3.5f);
	REQUIRE(morphDistance(0.0f, 1.5f) == 1.5f);
	REQUIRE(morphDistance(0.0f, 3.0f) == Catch::Approx(-1.0f));
	REQUIRE(morphDistance(3.9f, 0.1f) == Catch::Approx(0.2f));
	REQUIRE(morphDistance(0.1f, 3.9f) == Catch::Approx(-0.2f));
	REQUIRE(morphDistance(6.5f, 2.5f) == Catch::Approx(0.0f).margin(1.0e-6f));

	// The readout is the wrapped value, so the top of the range reads as the sine it is.
	vekt::mono::PluginProcessor processor;
	auto* parameter = processor.getParameters().getParameter(vekt::mono::parameters::osc1Morph);
	const auto text = [parameter](float morph) { return parameter->getText(parameter->convertTo0to1(morph), 32); };
	REQUIRE(text(4.0f) == "0.000");
	REQUIRE(text(3.9999998f) == "0.000");
	REQUIRE(text(3.5f) == "3.500");
	REQUIRE(text(0.0f) == "0.000");
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

	// A knob jump from sine to saw ramps linearly over 10 ms (480 samples).
	settings.morph[0] = 1.5f;
	voice.render(left, right, settings, 0.0f);
	REQUIRE(voice.getMorph(0) < 0.05f);
	for (int sample = 1; sample < 240; ++sample) voice.render(left, right, settings, 0.0f);
	REQUIRE(voice.getMorph(0) == Catch::Approx(0.75f).margin(0.02f));
	for (int sample = 240; sample < 480; ++sample) voice.render(left, right, settings, 0.0f);
	REQUIRE(voice.getMorph(0) == Catch::Approx(1.5f));

	// The ramp takes the short way round the cycle: sine to square goes back through the square-sine segment
	// (via 3.5), not forwards through triangle and saw.
	settings.morph[0] = 0.0f;
	for (int sample = 0; sample < 480; ++sample) voice.render(left, right, settings, 0.0f);
	REQUIRE(voice.getMorph(0) == Catch::Approx(0.0f).margin(1.0e-5f));
	settings.morph[0] = 3.0f;
	for (int sample = 0; sample < 240; ++sample) voice.render(left, right, settings, 0.0f);
	REQUIRE(voice.getMorph(0) == Catch::Approx(3.5f).margin(0.02f));
	for (int sample = 240; sample < 480; ++sample) voice.render(left, right, settings, 0.0f);
	REQUIRE(voice.getMorph(0) == Catch::Approx(3.0f));
	// Crossing the 4-to-0 wrap on the knob (3.9 to 0.1) moves only 0.2 and never passes through saw or triangle.
	settings.morph[0] = 3.9f;
	for (int sample = 0; sample < 480; ++sample) voice.render(left, right, settings, 0.0f);
	settings.morph[0] = 0.1f;
	for (int sample = 0; sample < 480; ++sample)
	{
		voice.render(left, right, settings, 0.0f);
		const auto morph = voice.getMorph(0);
		REQUIRE((morph >= 3.9f - 1.0e-4f || morph <= 0.1f + 1.0e-4f));
	}
	REQUIRE(voice.getMorph(0) == Catch::Approx(0.1f).margin(1.0e-5f));
	// A knob swept across the 4-to-0 wrap in small steps (3.8 ... 0.2), faster than the ramp settles, follows it
	// round and never jumps back towards saw or triangle.
	settings.morph[0] = 3.8f;
	for (int sample = 0; sample < 480; ++sample) voice.render(left, right, settings, 0.0f);
	for (const auto target : { 3.85f, 3.9f, 3.95f, 3.99f, 0.01f, 0.05f, 0.1f, 0.15f, 0.2f })
	{
		settings.morph[0] = target;
		for (int sample = 0; sample < 60; ++sample)
		{
			voice.render(left, right, settings, 0.0f);
			const auto morph = voice.getMorph(0);
			CAPTURE(target, morph);
			REQUIRE((morph >= 3.8f - 1.0e-4f || morph <= 0.2f + 1.0e-4f));
		}
	}
	for (int sample = 0; sample < 480; ++sample) voice.render(left, right, settings, 0.0f);
	REQUIRE(voice.getMorph(0) == Catch::Approx(0.2f).margin(1.0e-5f));
	settings.morph[0] = 3.0f;
	for (int sample = 0; sample < 480; ++sample) voice.render(left, right, settings, 0.0f);

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

TEST_CASE("Mono Width keeps aligned fundamentals, full-depth PWM and a level two-tooth saw", "[mono][oscillator][width]")
{
	const auto fundamentalPhase = [](float morph, float width)
	{
		double real {}, imaginary {};
		for (int sample = 0; sample < cycleSamples; ++sample)
		{
			const auto phase = static_cast<double>(sample) / cycleSamples;
			const auto value = static_cast<double>(oscillator(static_cast<float>(phase), morph, width));
			real += value * std::cos(2.0 * std::numbers::pi * phase);
			imaginary -= value * std::sin(2.0 * std::numbers::pi * phase);
		}
		return std::atan2(imaginary, real);
	};
	// Every anchor's fundamental stays at +sin phase (-pi/2), so Morph cannot cancel it.
	for (const auto width : { 5.0f, 20.0f, 49.0f, 51.0f, 80.0f, 95.0f })
		for (const auto morph : { 0.0f, 1.0f, 2.0f, 3.0f })
		{
			CAPTURE(width, morph);
			const auto error = std::remainder(fundamentalPhase(morph, width) + 0.5 * std::numbers::pi, 2.0 * std::numbers::pi);
			REQUIRE(std::abs(error) < 0.01);
		}
	// The square keeps full PWM: its duty cycle equals Width.
	for (const auto width : { 5.0f, 20.0f, 95.0f })
	{
		int high {};
		for (int sample = 0; sample < cycleSamples; ++sample)
			if (oscillator((static_cast<float>(sample) + 0.5f) / cycleSamples, 3.0f, width) > 0.0f) ++high;
		REQUIRE(static_cast<float>(high) / cycleSamples == Catch::Approx(width * 0.01f).margin(0.005f));
	}
	// Two-tooth saw at the extreme: a quarter-cycle offset removes H2 and keeps the saw's RMS.
	{
		double sum {}, h2Real {}, h2Imaginary {}, h1Real {}, h1Imaginary {};
		for (int sample = 0; sample < cycleSamples; ++sample)
		{
			const auto phase = static_cast<double>(sample) / cycleSamples;
			const auto value = static_cast<double>(oscillator(static_cast<float>(phase), 2.0f, 95.0f));
			sum += value * value;
			h1Real += value * std::cos(2.0 * std::numbers::pi * phase);
			h1Imaginary += value * std::sin(2.0 * std::numbers::pi * phase);
			h2Real += value * std::cos(4.0 * std::numbers::pi * phase);
			h2Imaginary += value * std::sin(4.0 * std::numbers::pi * phase);
		}
		REQUIRE(std::sqrt(sum / cycleSamples) == Catch::Approx(targetRms).margin(0.01));
		REQUIRE(std::hypot(h2Real, h2Imaginary) < 0.01 * std::hypot(h1Real, h1Imaginary));
	}
}

TEST_CASE("Mono Width offline reference matches the shipped Width model", "[mono][oscillator][width][reference]")
{
	// Independent Fourier coefficients (MonoWidthReference.h) versus a dense DFT of the production anchors.
	for (int anchor = 0; anchor < 4; ++anchor)
		for (const auto width : { 5.0f, 20.0f, 50.0f, 63.7f, 95.0f })
		{
			CAPTURE(anchor, width);
			const auto reference = vekt::audio_lab::monoWidthAnchorCoefficients(anchor, width);
			for (int harmonic = 0; harmonic <= 12; ++harmonic)
			{
				std::complex<double> actual {};
				for (int sample = 0; sample < cycleSamples; ++sample)
				{
					const auto phase = static_cast<double>(sample) / cycleSamples;
					actual += static_cast<double>(oscillator(static_cast<float>(phase), static_cast<float>(anchor), width))
						* std::polar(1.0, -2.0 * std::numbers::pi * harmonic * phase);
				}
				actual /= static_cast<double>(cycleSamples);
				CAPTURE(harmonic);
				REQUIRE(std::abs(actual - reference[static_cast<std::size_t>(harmonic)]) < 2.0e-3);
			}
		}
}

TEST_CASE("Mono Width knob jumps ramp instead of stepping the oscillator", "[mono][oscillator][width]")
{
	// A sustained sine through the open filter: its largest sample-to-sample change is small, so any
	// step from an instantaneous Width change (shape and DC both move) stands out. Without the ramp the
	// 5 -> 95 % jump steps by ~0.33 against a steady maximum of ~0.03.
	for (const auto [from, to] : { std::pair { 5.0f, 95.0f }, std::pair { 95.0f, 5.0f }, std::pair { 50.0f, 95.0f } })
	{
		CAPTURE(from, to);
		vekt::mono::MonoVoiceSettings settings {};
		settings.range.fill(1.0f);
		settings.level = { 1.0f, 0.0f, 0.0f };
		settings.morph.fill(0.0f);
		settings.pulseWidth.fill(from);
		settings.cutoff = 20'000.0f;
		settings.ampSustain = 1.0f;
		settings.unison = 1;
		vekt::mono::MonoVoice voice;
		voice.prepare(48'000.0, 7);
		voice.start(1, 57, 0.8f, settings, true, false, 1);
		float left {}, right {}, previous {};
		float steadyStep {};
		// render() adds into its outputs (the processor sums voices), so clear them per sample.
		for (int sample = 0; sample < 4'800; ++sample)
		{
			left = right = 0.0f;
			voice.render(left, right, settings, 0.0f);
			if (sample >= 2'400) steadyStep = std::max(steadyStep, std::abs(left - previous));
			previous = left;
		}
		settings.pulseWidth.fill(to);
		float jumpStep {};
		for (int sample = 0; sample < 960; ++sample)
		{
			left = right = 0.0f;
			voice.render(left, right, settings, 0.0f);
			jumpStep = std::max(jumpStep, std::abs(left - previous));
			previous = left;
		}
		// The new shape can itself be steeper, so compare with the larger of the two steady states.
		for (int sample = 0; sample < 2'400; ++sample)
		{
			left = right = 0.0f;
			voice.render(left, right, settings, 0.0f);
			steadyStep = std::max(steadyStep, std::abs(left - previous));
			previous = left;
		}
		CAPTURE(steadyStep, jumpStep);
		REQUIRE(jumpStep < 1.5f * steadyStep);
	}
}

TEST_CASE("Mono Drift walk is bounded, smooth and slow", "[mono][oscillator][drift]")
{
	constexpr float rate = 48'000.0f;
	const auto coefficient = 1.0f - std::exp(-1.0f / (0.6f * rate));
	juce::Random random(1234);
	vekt::mono::DriftWalk walk;
	walk.reset(random, rate);
	const auto start = static_cast<float>(walk.value);
	float previous = start, minimum = start, maximum = start, largestStep {};
	int signChanges {};
	for (int sample = 0; sample < static_cast<int>(60.0f * rate); ++sample)
	{
		const auto value = walk.next(random, rate, coefficient);
		largestStep = std::max(largestStep, std::abs(value - previous));
		if ((value > 0.0f) != (previous > 0.0f)) ++signChanges;
		minimum = std::min(minimum, value);
		maximum = std::max(maximum, value);
		previous = value;
	}
	REQUIRE(minimum >= -1.0f);
	REQUIRE(maximum <= 1.0f);
	REQUIRE(maximum - minimum > 0.5f);   // it actually wanders over a minute
	REQUIRE(largestStep < 1.0e-4f);      // no steps: ~0.5 Hz motion moves far less than this per sample
	REQUIRE(signChanges < 60);           // slow: well under one zero crossing per second
}

TEST_CASE("Mono Drift wanders each voice's pitch by a few cents", "[mono][oscillator][drift]")
{
	// A held A4 sine through the open filter; pitch measured from interpolated rising zero crossings in
	// half-second windows over 20 s.
	const auto pitchCents = [](float drift)
	{
		vekt::mono::MonoVoiceSettings settings {};
		settings.range.fill(1.0f);
		settings.level = { 1.0f, 0.0f, 0.0f };
		settings.morph.fill(0.0f);
		settings.pulseWidth.fill(50.0f);
		settings.cutoff = 20'000.0f;
		settings.ampSustain = 1.0f;
		settings.unison = 1;
		settings.drift = drift;
		MonoVoice voice;
		voice.prepare(48'000.0, 99);
		voice.start(1, 69, 0.8f, settings, true, false, 1);
		std::vector<float> windows;
		float previous {};
		double lastCrossing = -1.0;
		int crossings {};
		double firstCrossing {};
		constexpr int windowSamples = 24'000;
		for (int sample = 0; sample < 20 * 48'000; ++sample)
		{
			float left {}, right {};
			voice.render(left, right, settings, 0.0f);
			if (sample > 4'800 && previous <= 0.0f && left > 0.0f)
			{
				const auto crossing = static_cast<float>(sample - 1) + previous / (previous - left);
				if (crossings == 0) firstCrossing = crossing;
				lastCrossing = crossing;
				++crossings;
			}
			previous = left;
			if (sample > 4'800 && sample % windowSamples == 0 && crossings > 1)
			{
				const auto hz = (crossings - 1) * 48'000.0 / (lastCrossing - firstCrossing);
				windows.push_back(static_cast<float>(1'200.0 * std::log2(hz / 440.0)));
				crossings = 0;
			}
		}
		return windows;
	};
	const auto steady = pitchCents(0.0f);
	for (const auto cents : steady) REQUIRE(std::abs(cents) < 0.05f);
	const auto drifting = pitchCents(100.0f);
	const auto [low, high] = std::minmax_element(drifting.begin(), drifting.end());
	CAPTURE(*low, *high);
	REQUIRE(*high - *low > 5.0f);                                          // it moves clearly at full Drift
	REQUIRE(std::max(std::abs(*low), std::abs(*high)) < 40.5f);            // within 4 x +/-(7 + 3) ct plus measurement
	// Low settings stay subtle: the depth curve is progressive.
	REQUIRE(vekt::mono::driftAmount(10.0f) == Catch::Approx(0.103f));
	REQUIRE(vekt::mono::driftAmount(100.0f) == Catch::Approx(4.0f));
	REQUIRE(vekt::mono::driftSpeed(100.0f) == Catch::Approx(3.0f));
}

TEST_CASE("Mono unison level stays put while Drift separates the layers", "[mono][oscillator][drift]")
{
	// Four layers at zero Detune start identical; Drift's per-layer wander decorrelates them. The unison
	// gain must follow that separation, or the note sinks by up to 6 dB as it is held.
	vekt::mono::MonoVoiceSettings settings {};
	settings.range.fill(1.0f);
	settings.level = { 1.0f, 0.0f, 0.0f };
	settings.morph.fill(2.0f);
	settings.pulseWidth.fill(50.0f);
	settings.cutoff = 20'000.0f;
	settings.ampSustain = 1.0f;
	settings.unison = 4;
	settings.detune = 0.0f;
	settings.drift = 100.0f;
	MonoVoice voice;
	voice.prepare(48'000.0, 5);
	voice.start(1, 57, 0.8f, settings, true, false, 1);
	const auto windowRms = [&](int samples)
	{
		double sum {};
		for (int sample = 0; sample < samples; ++sample)
		{
			float left {}, right {};
			voice.render(left, right, settings, 0.0f);
			sum += 0.5 * (static_cast<double>(left) * left + static_cast<double>(right) * right);
		}
		return std::sqrt(sum / samples);
	};
	windowRms(2'400);
	const auto start = windowRms(4'800);   // 50-150 ms: layers still nearly identical
	windowRms(8 * 48'000);
	const auto held = windowRms(2 * 48'000); // after ~8 s of drifting apart
	const auto change = 20.0 * std::log10(held / start);
	CAPTURE(change);
	REQUIRE(std::abs(change) < 2.0);
}
