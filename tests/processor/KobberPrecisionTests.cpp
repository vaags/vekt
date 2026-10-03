#include <vekt/dsp/OversamplingQuality.h>

#include "ContourEnvelope.h"
#include "Glide.h"
#include "Lfo.h"
#include "KobberVoice.h"

#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

// Kobber's per-sample state at every internal rate up to the highest (ARCHITECTURE.md, DSP Contracts): states that step
// toward a target or accumulate a phase must neither stall nor drift as the steps shrink with the rate.

namespace
{
// The base rate and the internal rates Kobber reaches: 48 kHz, its Offline default (4x), 192 kHz x4 and the highest.
constexpr std::array internalRates { 48'000.0, 192'000.0, 768'000.0, vekt::dsp::maximumInternalSampleRate };

// Samples until `done` holds, or -1 past `limit` seconds.
template <typename Step>
std::int64_t samplesUntil(double rate, double limitSeconds, Step&& done)
{
	const auto limit = static_cast<std::int64_t>(limitSeconds * rate);
	for (std::int64_t sample = 0; sample < limit; ++sample)
		if (done()) return sample;
	return -1;
}
}

TEST_CASE("Kobber contours finish their longest stages on time at every internal rate", "[kobber][contour][precision][slow]")
{
	// Times denote 99 % of the distance and the endpoint snaps at 99.99 %, so attack ends after its time and decay and
	// release after twice theirs. 5 % margin; a stalled stage never ends. The longest settings: Attack 10 s, Decay and
	// Release 20 s.
	using vekt::kobber::ContourEnvelope;
	for (const auto rate : internalRates)
	{
		CAPTURE(rate);
		ContourEnvelope attack;
		attack.setSampleRate(rate);
		attack.setParameters({ 10.0f, 1.0f, 0.5f, 1.0f });
		attack.noteOn();
		const auto attackSamples = samplesUntil(rate, 10.5, [&] { return attack.getNextSample() >= 1.0f; });
		CHECK(attackSamples > static_cast<std::int64_t>(9.5 * rate));

		ContourEnvelope decay;
		decay.setSampleRate(rate);
		decay.setParameters({ 0.0f, 20.0f, 0.25f, 1.0f });
		decay.noteOn();
		const auto decaySamples = samplesUntil(rate, 42.0, [&] { return decay.getNextSample() <= 0.25f; });
		CHECK(decaySamples > static_cast<std::int64_t>(38.0 * rate));

		ContourEnvelope release;
		release.setSampleRate(rate);
		release.setParameters({ 0.0f, 0.0f, 1.0f, 20.0f });
		release.noteOn();
		static_cast<void>(release.getNextSample());
		release.noteOff();
		const auto releaseSamples = samplesUntil(rate, 42.0, [&] { static_cast<void>(release.getNextSample()); return !release.isActive(); });
		CHECK(releaseSamples > static_cast<std::int64_t>(38.0 * rate));
	}
}

TEST_CASE("Kobber glide reaches its note at every internal rate", "[kobber][glide][precision]")
{
	// An octave glide, 1 s and 5 s at 48 kHz (5 s is the longest Glide Time), 1 s at the higher rates (cost): after eight time
	// constants it must be within 1 cent (e^-8 of 1,200 cents is 0.4 cent). In single precision it stopped up to
	// 6 semitones short at the highest rate.
	for (const auto rate : internalRates)
		for (const auto seconds : rate < 50'000.0 ? std::vector { 1.0f, 5.0f } : std::vector { 1.0f })
		{
			CAPTURE(rate, seconds);
			vekt::kobber::Glide glide;
			glide.jump(48.0f);
			glide.retarget(60.0f);
			double note {};
			for (std::int64_t sample = 0; sample < static_cast<std::int64_t>(8.0 * seconds * rate); ++sample)
				note = glide.next(true, seconds, static_cast<float>(rate));
			CHECK(std::abs(note - 60.0) * 100.0 < 1.0);
		}
}

TEST_CASE("Kobber drift walks reach their targets at every internal rate", "[kobber][drift][precision]")
{
	// The walk is two one-poles at the slowest Drift speed (time constant 0.6 s each); after 6 s (ten time constants)
	// it is within 0.1 % of a target one away.
	for (const auto rate : internalRates)
	{
		CAPTURE(rate);
		juce::Random random(7);
		vekt::kobber::DriftWalk walk;
		walk.value = walk.stage = 0.0f;
		walk.target = 1.0f;
		walk.samplesToNextTarget = std::numeric_limits<int>::max();
		const auto coefficient = 1.0 - std::exp(-1.0 / (0.6 * rate));
		float value {};
		for (std::int64_t sample = 0; sample < static_cast<std::int64_t>(6.0 * rate); ++sample)
			value = walk.next(random, rate, coefficient);
		CHECK(std::abs(1.0f - value) < 1.0e-3f);
	}
}

TEST_CASE("Kobber LFO fades in on time at every internal rate", "[kobber][lfo][precision]")
{
	// A 10 s fade of a held unipolar square (1 for its first half cycle at the lowest rate) reaches full level after
	// 10 s within 1 %.
	for (const auto rate : internalRates)
	{
		CAPTURE(rate);
		vekt::kobber::Lfo lfo;
		lfo.setSampleRate(rate);
		vekt::kobber::Lfo::Parameters parameters;
		parameters.rateHz = vekt::kobber::minimumLfoRateHz;
		parameters.shape = vekt::kobber::LfoShape::square;
		parameters.polarity = vekt::kobber::LfoPolarity::unipolar;
		parameters.mode = vekt::kobber::LfoMode::retrigger;
		parameters.fadeSeconds = 10.0f;
		lfo.setParameters(parameters);
		lfo.reset(1, 2);
		lfo.noteOn();
		const auto samples = samplesUntil(rate, 11.0, [&] { return lfo.getNextSample(0.0) >= 1.0f; });
		CAPTURE(static_cast<double>(samples) / rate);
		CHECK(std::abs(static_cast<double>(samples) / rate - 10.0) < 0.1);
	}
}

TEST_CASE("Kobber low notes keep their pitch at the highest internal rate", "[kobber][oscillator][precision][slow]")
{
	// MIDI 24 with Octave -2 is 8.18 Hz, the lowest pitch a key without bend reaches. A sine through the open filter,
	// pitch from interpolated rising zero crossings over four cycles; within 1 cent (single-precision phase was up to
	// 19 cents off).
	for (const auto rate : { 48'000.0, vekt::dsp::maximumInternalSampleRate })
	{
		CAPTURE(rate);
		vekt::kobber::KobberVoiceSettings settings {};
		settings.rangeOctaves.fill(0); // 8'
		settings.octave = { -2.0f, 0.0f, 0.0f };
		settings.level = { 1.0f, 0.0f, 0.0f };
		settings.morph.fill(0.0f);
		settings.pulseWidth.fill(50.0f);
		settings.cutoff = 20'000.0f;
		settings.ampSustain = 1.0f;
		settings.unison = 1;
		vekt::kobber::KobberVoice voice;
		voice.prepare(rate, 99);
		voice.start(1, 24, 0.8f, settings, true, false, 1);
		const auto expected = 440.0 * std::exp2((24.0 - 69.0) / 12.0) / 4.0;
		float previous {};
		double firstCrossing = -1.0, lastCrossing {};
		int crossings {};
		const auto settle = static_cast<std::int64_t>(0.2 * rate);
		for (std::int64_t sample = 0; sample < settle + static_cast<std::int64_t>(4.6 / expected * rate); ++sample)
		{
			float left {}, right {};
			voice.render(left, right, settings, 0.0f);
			if (sample > settle && previous <= 0.0f && left > 0.0f)
			{
				const auto crossing = static_cast<double>(sample - 1) + previous / (previous - left);
				if (firstCrossing < 0.0) firstCrossing = crossing;
				lastCrossing = crossing;
				++crossings;
			}
			previous = left;
		}
		REQUIRE(crossings >= 4);
		const auto hz = (crossings - 1) * rate / (lastCrossing - firstCrossing);
		const auto cents = 1'200.0 * std::log2(hz / expected);
		CAPTURE(hz, expected, cents);
		CHECK(std::abs(cents) < 1.0);
	}
}
