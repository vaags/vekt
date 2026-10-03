#pragma once

#include <vekt/dsp/LinearTptSvf.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace vekt::flint
{
// SplitMix64: a counter-based generator, so a hit's values depend only on its hash and draw index.
[[nodiscard]] constexpr std::uint64_t splitMix64(std::uint64_t value) noexcept
{
	value += 0x9e3779b97f4a7c15ULL;
	value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
	value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
	return value ^ (value >> 31);
}

// A hit's hash (docs/FLINT_VALIDATION.md, Shared stages): the instance seed, the hit's song position rounded to 1/3840
// beat, and its order among hits on the same sample. With the transport stopped, a per-instance counter replaces the
// position (in its own domain, so a counter value never equals a song position).
inline constexpr double hitGridPerBeat = 3'840.0;

[[nodiscard]] inline std::int64_t songPositionKey(double beats) noexcept
{
	return std::llround(beats * hitGridPerBeat);
}

[[nodiscard]] constexpr std::uint64_t hitHash(
    std::uint64_t seed, std::int64_t positionKey, std::uint32_t order) noexcept
{
	return splitMix64(seed ^ splitMix64(static_cast<std::uint64_t>(positionKey) ^ (std::uint64_t { order } << 48)));
}

[[nodiscard]] constexpr std::uint64_t stoppedHitHash(std::uint64_t seed, std::uint64_t counter) noexcept
{
	return splitMix64(splitMix64(seed ^ 0x53746f70ULL) ^ counter); // "Stop"
}

// A hit's random values (docs/FLINT_VALIDATION.md, Shared stages): draw i is splitMix64(hash + i), in each model's
// documented draw order.
class HitRandom final
{
public:
	explicit HitRandom(std::uint64_t hitHash) noexcept : hash(hitHash) {}

	// Uniform in [0, 1).
	[[nodiscard]] double uniform() noexcept
	{
		return static_cast<double>(splitMix64(hash + index++) >> 11) * 0x1.0p-53;
	}

	// Approximately normal in [-1, 1]: the mean of three uniform draws in [-1, 1].
	[[nodiscard]] double bell() noexcept
	{
		return ((2.0 * uniform() - 1.0) + (2.0 * uniform() - 1.0) + (2.0 * uniform() - 1.0)) / 3.0;
	}

	[[nodiscard]] std::uint64_t seed() noexcept { return splitMix64(hash + index++); }

private:
	std::uint64_t hash;
	std::uint64_t index {};
};

// White noise in [-1, 1) for a hit's noise: a fixed realization (constant seed) and a fresh one (seeded from the hit)
// mixed at equal power with the fresh share equal to Variation, low-passed at 18 kHz and scaled so its level is the
// same at every internal sample rate (docs/FLINT_VALIDATION.md, Variation): at a high internal rate it adds nothing
// above the audible band to feed Drive.
class HitNoise final
{
public:
	static constexpr std::uint64_t fixedSeed = 0x466c696e74ULL; // "Flint"
	static constexpr double referenceSampleRate = 44'100.0;

	void prepare(double sampleRate) noexcept
	{
		// Scale white noise so its power after the band limit matches an ideal 18 kHz second-order Butterworth's on
		// 44.1 kHz noise: the filter's own noise gain, its impulse response's energy, is measured here, so the level is
		// the same whatever the rate (near Nyquist the prewarped filter passes less than the ideal one).
		bandLimit.prepare(sampleRate);
		auto energy = 0.0;
		auto input = 1.0;
		for (auto index = 0; index < 8'192; ++index)
		{
			const auto response = bandLimit.process(input, bandLimitHz, std::numbers::sqrt2).lowPass;
			energy += response * response;
			input = 0.0;
		}
		bandLimit.reset();
		constexpr auto idealBandwidth = std::numbers::pi / 4.0 / 0.7071067811865476 * bandLimitHz; // 1.11 fc
		rateScale = std::sqrt(idealBandwidth / (0.5 * referenceSampleRate) / energy);
	}

	void start(std::uint64_t freshSeed, double variation) noexcept
	{
		fixedState = splitMix64(fixedSeed);
		freshState = splitMix64(freshSeed) | 1ULL;
		bandLimit.reset();
		const auto share = std::clamp(variation, 0.0, 1.0);
		freshShare = std::sqrt(share);
		fixedShare = std::sqrt(1.0 - share);
	}

	[[nodiscard]] double next() noexcept
	{
		const auto fixed = fixedShare > 0.0 ? sample(fixedState) : 0.0;
		const auto fresh = freshShare > 0.0 ? sample(freshState) : 0.0;
		const auto white = fixedShare * fixed + freshShare * fresh;
		return rateScale * bandLimit.process(white, bandLimitHz, std::numbers::sqrt2).lowPass;
	}

private:
	[[nodiscard]] static double sample(std::uint64_t& state) noexcept
	{
		// xorshift64*
		state ^= state >> 12;
		state ^= state << 25;
		state ^= state >> 27;
		const auto value = state * 0x2545f4914f6cdd1dULL;
		return static_cast<double>(value >> 11) * 0x1.0p-52 - 1.0;
	}

	std::uint64_t fixedState { 1 };
	std::uint64_t freshState { 1 };
	double fixedShare { 1.0 };
	double freshShare {};
	double rateScale { 1.0 };
	dsp::LinearTptSvf bandLimit;
	static constexpr double bandLimitHz = 18'000.0;
};
}
