#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <utility>

namespace vekt::mono
{
enum class LfoShape { sine, triangle, sawUp, sawDown, square, smoothRandom };
enum class LfoPolarity { bipolar, unipolar };
enum class LfoMode { free, retrigger, oneShot };

inline constexpr float minimumLfoRateHz = 0.01f;
inline constexpr float maximumLfoRateHz = 50.0f;

// Tempo-sync divisions as parameter choices, each with its cycle length in quarter-note beats.
inline constexpr std::array<std::pair<const char*, double>, 15> lfoDivisions { {
	{ "1/1", 4.0 }, { "1/1 D", 6.0 }, { "1/1 T", 8.0 / 3.0 },
	{ "1/2", 2.0 }, { "1/2 D", 3.0 }, { "1/2 T", 4.0 / 3.0 },
	{ "1/4", 1.0 }, { "1/4 D", 1.5 }, { "1/4 T", 2.0 / 3.0 },
	{ "1/8", 0.5 }, { "1/8 D", 0.75 }, { "1/8 T", 1.0 / 3.0 },
	{ "1/16", 0.25 }, { "1/16 D", 0.375 }, { "1/16 T", 1.0 / 6.0 } } };
inline constexpr int defaultLfoDivision = 6;

[[nodiscard]] inline double lfoDivisionBeats(int division) noexcept
{
	return lfoDivisions[static_cast<std::size_t>(std::clamp(division, 0, static_cast<int>(lfoDivisions.size()) - 1))].second;
}

[[nodiscard]] inline float syncedLfoRateHz(double beatsPerMinute, int division) noexcept
{
	return static_cast<float>(beatsPerMinute / 60.0 / lfoDivisionBeats(division));
}

// Free-running cycle counter shared by every voice's LFO in Free mode, so all
// voices move together. Tempo sync sets its position from the host timeline.
class LfoClock
{
public:
	void setSampleRate(double rate) noexcept { sampleRate = rate > 0.0 ? rate : 48'000.0; }
	void setRate(float hz) noexcept { increment = std::clamp(hz, minimumLfoRateHz, maximumLfoRateHz) / sampleRate; }
	void setPosition(double cycles) noexcept { position = cycles; }
	void reset() noexcept { position = 0.0; }
	void advance() noexcept { position += increment; }
	[[nodiscard]] double getPosition() const noexcept { return position; }

private:
	double sampleRate { 48'000.0 }, increment { 1.0 / 48'000.0 }, position {};
};

// Per-voice LFO. Values are -1..1 (bipolar) or 0..1 (unipolar), already scaled
// by the note's delay/fade gain; depth and routing are applied by the caller.
class Lfo
{
public:
	struct Parameters
	{
		float rateHz { 1.0f };
		LfoShape shape { LfoShape::sine };
		LfoPolarity polarity { LfoPolarity::bipolar };
		LfoMode mode { LfoMode::free };
		float phase {};          // Start phase (Retrigger/One Shot) or offset from the shared clock (Free), in cycles.
		float delaySeconds {};   // Silent time after a note starts.
		float fadeSeconds {};    // Linear fade-in after the delay.
		float drift {};          // 0..1: subtle per-voice rate, phase, symmetry and level variation.
	};

	void setSampleRate(double rate) noexcept { sampleRate = rate > 0.0 ? rate : 48'000.0; }
	void setParameters(const Parameters& next) noexcept
	{
		parameters = next;
		parameters.rateHz = std::clamp(next.rateHz, minimumLfoRateHz, maximumLfoRateHz);
		parameters.drift = std::clamp(next.drift, 0.0f, 1.0f);
	}

	// voiceSeed varies per voice; sharedSeed is per LFO slot so Free-mode random is common to all voices.
	void reset(std::uint32_t voiceSeed, std::uint32_t sharedSeed) noexcept
	{
		seed = voiceSeed;
		commonSeed = sharedSeed;
		driftRate = unitRandom(voiceSeed, 1);
		driftPhase = unitRandom(voiceSeed, 2);
		driftSymmetry = unitRandom(voiceSeed, 3);
		driftLevel = unitRandom(voiceSeed, 4);
		cycles = 0.0;
		delaySamplesRemaining = 0;
		fadeGain = 0.0;
		fadeIncrement = 0.0;
		finished = false;
		lastValue = 0.0f;
	}

	void noteOn() noexcept
	{
		cycles = 0.0;
		finished = false;
		delaySamplesRemaining = static_cast<std::int64_t>(std::llround(std::max(0.0f, parameters.delaySeconds) * sampleRate));
		const auto fadeSamples = static_cast<double>(std::max(0.0f, parameters.fadeSeconds)) * sampleRate;
		fadeIncrement = fadeSamples >= 1.0 ? 1.0 / fadeSamples : 1.0;
		fadeGain = 0.0;
	}

	[[nodiscard]] float getNextSample(double sharedClockPosition) noexcept
	{
		const auto freeRunning = parameters.mode == LfoMode::free;
		if (delaySamplesRemaining > 0)
		{
			--delaySamplesRemaining;
			return 0.0f;
		}
		fadeGain = std::min(1.0, fadeGain + fadeIncrement);
		if (!freeRunning && finished) return lastValue * static_cast<float>(fadeGain);

		const auto driftAmount = parameters.drift;
		const auto phaseOffset = static_cast<double>(parameters.phase) + static_cast<double>(driftAmount * maximumPhaseDrift * driftPhase);
		const auto position = (freeRunning ? sharedClockPosition : cycles) + phaseOffset;
		auto value = shapeValue(position, freeRunning ? commonSeed : seed);
		if (parameters.polarity == LfoPolarity::unipolar) value = 0.5f * (value + 1.0f);
		value *= 1.0f + driftAmount * maximumLevelDrift * driftLevel;
		lastValue = value;

		if (!freeRunning)
		{
			const auto rate = parameters.rateHz * (1.0f + driftAmount * maximumRateDrift * driftRate);
			cycles += static_cast<double>(rate) / sampleRate;
			// Tolerate accumulated rounding so the cycle cannot overrun by a sample.
			if (parameters.mode == LfoMode::oneShot && cycles >= 1.0 - 1.0e-9) finished = true;
		}
		return value * static_cast<float>(fadeGain);
	}

	[[nodiscard]] bool isFinished() const noexcept { return finished; }

	// Waveform at an absolute cycle position (integer part selects the random segment), bipolar.
	[[nodiscard]] static float bipolarShape(LfoShape shape, double position, std::uint32_t randomSeed, float symmetry = 0.5f) noexcept
	{
		const auto cycle = std::floor(position);
		const auto phase = warp(static_cast<float>(position - cycle), symmetry);
		switch (shape)
		{
		case LfoShape::sine: return std::sin(2.0f * std::numbers::pi_v<float> * phase);
		case LfoShape::triangle:
			return phase < 0.25f ? 4.0f * phase : phase < 0.75f ? 2.0f - 4.0f * phase : 4.0f * phase - 4.0f;
		case LfoShape::sawUp: return 2.0f * phase - 1.0f;
		case LfoShape::sawDown: return 1.0f - 2.0f * phase;
		case LfoShape::square: return phase < 0.5f ? 1.0f : -1.0f;
		case LfoShape::smoothRandom:
		{
			// One random target per cycle, joined with a cosine curve so the output never steps.
			const auto segment = static_cast<std::int64_t>(cycle);
			const auto from = unitRandom(randomSeed, segment);
			const auto to = unitRandom(randomSeed, segment + 1);
			const auto blend = 0.5f - 0.5f * std::cos(std::numbers::pi_v<float> * phase);
			return from + (to - from) * blend;
		}
		}
		return 0.0f;
	}

private:
	static constexpr float maximumRateDrift = 0.02f;
	static constexpr float maximumPhaseDrift = 0.02f;
	static constexpr float maximumSymmetryDrift = 0.03f;
	static constexpr float maximumLevelDrift = 0.03f;

	[[nodiscard]] float shapeValue(double position, std::uint32_t randomSeed) const noexcept
	{
		const auto symmetry = 0.5f + parameters.drift * maximumSymmetryDrift * driftSymmetry;
		return bipolarShape(parameters.shape, position, randomSeed, symmetry);
	}

	// Moves the half-cycle point to `symmetry` while keeping 0 and 1 fixed.
	[[nodiscard]] static float warp(float phase, float symmetry) noexcept
	{
		if (symmetry == 0.5f) return phase;
		return phase < symmetry ? 0.5f * phase / symmetry : 0.5f + 0.5f * (phase - symmetry) / (1.0f - symmetry);
	}

	// Deterministic -1..1 value for (seed, index), so random segments need no shared generator state.
	[[nodiscard]] static float unitRandom(std::uint32_t randomSeed, std::int64_t index) noexcept
	{
		auto x = static_cast<std::uint64_t>(index) * 0x9e3779b97f4a7c15ull ^ (static_cast<std::uint64_t>(randomSeed) << 32 | randomSeed);
		x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ull;
		x ^= x >> 27; x *= 0x94d049bb133111ebull;
		x ^= x >> 31;
		return static_cast<float>(static_cast<double>(x >> 11) * 0x1.0p-53 * 2.0 - 1.0);
	}

	Parameters parameters;
	double sampleRate { 48'000.0 }, cycles {};
	std::int64_t delaySamplesRemaining {};
	double fadeGain {}, fadeIncrement {}; // double so a long fade keeps its time at every internal rate
	float lastValue {};
	float driftRate {}, driftPhase {}, driftSymmetry {}, driftLevel {};
	std::uint32_t seed {}, commonSeed {};
	bool finished {};
};
}
