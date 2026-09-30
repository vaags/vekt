#pragma once

#include "MonoWidthPitchLevels.h"

#include "MonoVoice.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

namespace vekt::audio_lab
{
// Analytic Fourier coefficients of the shipped Width model: the production formulas
// (vekt::mono::widthHarmonics), checked against the independent FFT reference in the tests.
inline void monoWidthAnalyticAnchorRange(int anchor, double width, int first, int last,
	std::complex<double>* out, bool align = true)
{
	vekt::mono::widthHarmonics(anchor, width, first, last, out, align);
}

inline void monoWidthAnalyticAnchorCoefficients(int anchor, double width, std::size_t count,
	std::complex<double>* out, bool align = true)
{
	if (count > 0) monoWidthAnalyticAnchorRange(anchor, width, 0, static_cast<int>(count) - 1, out, align);
}

// Offline exact additive oscillator: the shipped Width/Morph harmonics with one of the offline
// pitch-level policies applied per harmonic. The reference for the production table oscillator. Coefficients are recomputed only when (Morph, Width) change;
// a small cache covers three oscillators, but fast Width/Morph modulation is CPU-heavy. Unfaded
// harmonics are summed in float over eight vectorizable lanes; only faded ones take a gain.
class MonoAdditiveOscillator final
{
public:
	enum class Policy { perHarmonic, fourBandsPerOctave, twoBandsPerOctave };
	static constexpr int topHarmonic = 1023; // capacity of a 2048-sample table and of the level schedules
	static constexpr int lanes = 8; // interleaved power chains in the harmonic sum

	MonoAdditiveOscillator()
	{
		for (auto& entry : cache)
		{
			entry.coefficients.resize(topHarmonic + 1);
			entry.re.resize(topHarmonic + 1 + lanes);
			entry.im.resize(topHarmonic + 1 + lanes);
		}
		for (std::size_t schedule = 0; schedule < 2; ++schedule)
		{
			const auto limits = monoWidthLevelLimits(topHarmonic, schedule == 0 ? 4.0 : 2.0);
			auto& tops = groupTops[schedule];
			tops.resize(topHarmonic + 1);
			for (int harmonic = 1; harmonic <= topHarmonic; ++harmonic)
				tops[static_cast<std::size_t>(harmonic)] = *std::upper_bound(limits.rbegin(), limits.rend(), harmonic - 1);
		}
	}

	void setPolicy(Policy newPolicy) noexcept { policy.store(newPolicy, std::memory_order_relaxed); }
	// The guard is defined at the host rate, whatever the oscillator's internal rate.
	void setHostRate(double rate) noexcept { hostRate.store(rate, std::memory_order_relaxed); }

	float sample(float phase, float frequencyHz, float morph, float width) noexcept
	{
		const auto& entry = coefficientsFor(morph, width);
		const auto rate = hostRate.load(std::memory_order_relaxed);
		const auto pitch = static_cast<double>(std::abs(frequencyHz));
		auto value = static_cast<double>(entry.dc);
		if (pitch <= 0.0) return static_cast<float>(value);
		const auto count = std::min(topHarmonic, static_cast<int>(std::ceil(0.9 * rate / (2.0 * pitch))));
		const auto active = policy.load(std::memory_order_relaxed);
		const auto* tops = active == Policy::perHarmonic ? nullptr
			: groupTops[active == Policy::fourBandsPerOctave ? 0 : 1].data();
		// Harmonics up to flatEnd have gain 1 under the active policy (their gain harmonic is at or
		// below 0.8 of host Nyquist); group tops never decrease, so scan down from the limit.
		const auto limit = 0.4 * rate / pitch;
		auto flatEnd = std::min(count, static_cast<int>(std::floor(limit)));
		if (tops != nullptr)
			while (flatEnd > 0 && tops[flatEnd] > limit) --flatEnd;
		// Interleaved power chains z^(h..h+lanes-1), advanced by z^lanes: independent lanes the compiler
		// can vectorize. Float is ample here: each chain takes at most 1024 / lanes steps.
		const auto angle = 2.0 * std::numbers::pi * static_cast<double>(phase);
		const auto unitRe = std::cos(angle), unitIm = std::sin(angle);
		std::array<float, static_cast<std::size_t>(lanes)> powerRe {}, powerIm {}, sum {};
		auto zRe = unitRe, zIm = unitIm; // z^(lane + 1), in double
		for (std::size_t lane = 0; lane < static_cast<std::size_t>(lanes); ++lane)
		{
			powerRe[lane] = static_cast<float>(zRe);
			powerIm[lane] = static_cast<float>(zIm);
			const auto nextRe = zRe * unitRe - zIm * unitIm;
			zIm = zRe * unitIm + zIm * unitRe;
			zRe = nextRe;
		}
		// zRe/zIm now hold z^(lanes + 1); the lane step is z^lanes = that * conj(z).
		const auto stepRe = static_cast<float>(zRe * unitRe + zIm * unitIm);
		const auto stepIm = static_cast<float>(zIm * unitRe - zRe * unitIm);
		const auto* re = entry.re.data();
		const auto* im = entry.im.data();
		// Unfaded harmonics in whole blocks of four, then the rest (every faded one) with gains;
		// gains past `count` are zero, so the last block may run into the zero padding.
		const auto flatBlocksEnd = 1 + (flatEnd / lanes) * lanes; // first harmonic after the whole blocks
		for (auto harmonic = 1; harmonic < flatBlocksEnd; harmonic += lanes)
			for (std::size_t lane = 0; lane < static_cast<std::size_t>(lanes); ++lane)
			{
				const auto index = static_cast<std::size_t>(harmonic) + lane;
				sum[lane] += re[index] * powerRe[lane] - im[index] * powerIm[lane];
				const auto nextRe = powerRe[lane] * stepRe - powerIm[lane] * stepIm;
				powerIm[lane] = powerRe[lane] * stepIm + powerIm[lane] * stepRe;
				powerRe[lane] = nextRe;
			}
		const auto tailEnd = flatBlocksEnd + ((count - flatBlocksEnd) / lanes + 1) * lanes;
		// Grouped policies share one gain per level group, so evaluate it once per group top.
		auto lastTop = -1;
		auto lastGain = 0.0f;
		for (auto harmonic = flatBlocksEnd; harmonic < tailEnd; ++harmonic)
		{
			auto gain = 0.0f;
			if (harmonic <= flatEnd) gain = 1.0f;
			else if (harmonic <= count)
			{
				const auto top = tops == nullptr ? harmonic : tops[harmonic];
				if (top != lastTop)
				{
					lastTop = top;
					lastGain = static_cast<float>(monoWidthGuardGain(top, pitch, rate));
				}
				gain = lastGain;
			}
			tailGains[static_cast<std::size_t>(harmonic)] = gain;
		}
		for (auto harmonic = flatBlocksEnd; harmonic < tailEnd; harmonic += lanes)
			for (std::size_t lane = 0; lane < static_cast<std::size_t>(lanes); ++lane)
			{
				const auto index = static_cast<std::size_t>(harmonic) + lane;
				sum[lane] += tailGains[index] * (re[index] * powerRe[lane] - im[index] * powerIm[lane]);
				const auto nextRe = powerRe[lane] * stepRe - powerIm[lane] * stepIm;
				powerIm[lane] = powerRe[lane] * stepIm + powerIm[lane] * stepRe;
				powerRe[lane] = nextRe;
			}
		for (const auto partial : sum) value += static_cast<double>(partial);
		return static_cast<float>(value);
	}

private:
	struct Entry
	{
		float morph { -1.0f }, width { -1.0f }, dc {};
		std::vector<std::complex<double>> coefficients;
		std::vector<float> re, im; // 2 * c[h], split for the vectorized sum, zero-padded by one block
	};

	const Entry& coefficientsFor(float morph, float width) noexcept
	{
		// Quantize so slow modulation reuses entries instead of recomputing every sample.
		const auto keyMorph = std::round(morph * 1000.0f) / 1000.0f;
		const auto keyWidth = std::round(width * 100.0f) / 100.0f;
		for (auto& entry : cache)
			if (std::abs(entry.morph - keyMorph) < 1.0e-6f && std::abs(entry.width - keyWidth) < 1.0e-6f) return entry;
		auto& entry = cache[next];
		next = (next + 1) % cache.size();
		entry.morph = keyMorph;
		entry.width = keyWidth;
		const auto blend = monoWidthMorphBlend(keyMorph, vekt::mono::squareSineMorphPower());
		monoWidthAnalyticAnchorCoefficients(blend.from, keyWidth, entry.coefficients.size(), entry.coefficients.data());
		if (blend.weight > 0.0)
		{
			monoWidthAnalyticAnchorCoefficients(blend.to, keyWidth, scratch.size(), scratch.data());
			for (std::size_t index = 0; index < scratch.size(); ++index)
				entry.coefficients[index] += blend.weight * (scratch[index] - entry.coefficients[index]);
		}
		entry.dc = static_cast<float>(entry.coefficients[0].real());
		for (std::size_t index = 0; index < entry.coefficients.size(); ++index)
		{
			entry.re[index] = static_cast<float>(2.0 * entry.coefficients[index].real());
			entry.im[index] = static_cast<float>(2.0 * entry.coefficients[index].imag());
		}
		return entry;
	}

	std::atomic<Policy> policy { Policy::fourBandsPerOctave };
	std::atomic<double> hostRate { 48'000.0 };
	std::array<std::vector<int>, 2> groupTops;
	std::array<Entry, 8> cache;
	std::size_t next {};
	std::array<float, static_cast<std::size_t>(topHarmonic + 1 + lanes)> tailGains {}; // audio-thread scratch
	std::vector<std::complex<double>> scratch = std::vector<std::complex<double>>(topHarmonic + 1);
};
}
