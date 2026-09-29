#pragma once

#include "MonoWidthReference.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace vekt::audio_lab
{
// Offline pitch-level analysis in coefficient space. A grouped level bank
// that telescopes adjacent-level differences (MonoWidthTablePrototype with
// explicit limits) applies to every harmonic in (limit[l+1], limit[l]] the
// guard gain of limit[l], i.e. the group fades with its highest member. This
// isolates the level-spacing decision from phase/Width lookup error and from
// any finite first level, so it can sweep the whole oscillator pitch range.

// Descending harmonic limits from topHarmonic to 1, then 0 (DC only).
// Each step divides by 2^(1/bandsPerOctave), removing at least one partial;
// a very large bandsPerOctave gives the one-harmonic-per-level ladder.
inline std::vector<int> monoWidthLevelLimits(int topHarmonic, double bandsPerOctave)
{
	std::vector<int> limits;
	for (auto harmonic = topHarmonic; harmonic > 0;
		harmonic = std::min(harmonic - 1, static_cast<int>(std::floor(harmonic / std::exp2(1.0 / bandsPerOctave)))))
		limits.push_back(harmonic);
	limits.push_back(0);
	return limits;
}

// Gain the grouped bank applies to one harmonic; zero above its first level.
inline double monoWidthGroupedGain(int harmonic, const std::vector<int>& limits, double pitch,
	double sampleRate, double start = 0.8, double end = 0.9)
{
	if (harmonic > limits.front()) return 0.0;
	// limits is descending: the group top is the smallest limit >= harmonic.
	const auto top = std::upper_bound(limits.rbegin(), limits.rend(), harmonic - 1);
	return monoWidthGuardGain(*top, pitch, sampleRate, start, end);
}

struct MonoWidthLevelError
{
	double errorPower {}, signalPower {};
	// Lowest partial, as a fraction of host Nyquist, that the grouped bank
	// attenuates by more than 1 dB beyond the guarded target (0 when none).
	double lowestDarkenedRatio {};
};

// Static error of a grouped bank against the guarded target. coefficients
// are c[0..H] of the ideal waveform with H covering every nonzero guard gain.
inline MonoWidthLevelError monoWidthLevelError(const std::vector<std::complex<double>>& coefficients,
	const std::vector<int>& limits, double pitch, double sampleRate, double start = 0.8, double end = 0.9)
{
	MonoWidthLevelError result;
	result.signalPower = std::norm(coefficients.front());
	const auto nyquist = sampleRate / 2.0;
	for (std::size_t harmonic = 1; harmonic < coefficients.size(); ++harmonic)
	{
		const auto h = static_cast<int>(harmonic);
		const auto target = monoWidthGuardGain(h, pitch, sampleRate, start, end);
		if (target <= 0.0) break; // gains are monotonic in harmonic number
		jassert(h <= limits.front());
		const auto grouped = monoWidthGroupedGain(h, limits, pitch, sampleRate, start, end);
		const auto power = 2.0 * std::norm(coefficients[harmonic]);
		result.signalPower += power * target * target;
		result.errorPower += power * (target - grouped) * (target - grouped);
		if (power > 1.0e-20 && grouped < target * 0.891250938 && result.lowestDarkenedRatio <= 0.0)
			result.lowestDarkenedRatio = h * pitch / nyquist;
	}
	return result;
}

// Ideal coefficients at any Morph are the Morph-weighted blend of the two
// adjacent anchors' coefficients (the reference is linear in its anchors).
inline std::vector<std::complex<double>> monoWidthBlendCoefficients(
	const std::vector<std::vector<std::complex<double>>>& anchors, double morph)
{
	const auto segment = std::clamp(static_cast<int>(morph), 0, 2);
	const auto fraction = morph - segment;
	const auto sawWeight = [](double t) { return t * t * (2.0 - t); };
	const auto weight = segment == 0 ? fraction : segment == 1 ? sawWeight(fraction) : 1.0 - sawWeight(1.0 - fraction);
	const auto& from = anchors[static_cast<std::size_t>(segment)];
	const auto& to = anchors[static_cast<std::size_t>(segment + 1)];
	std::vector<std::complex<double>> result(std::min(from.size(), to.size()));
	for (std::size_t index = 0; index < result.size(); ++index)
		result[index] = from[index] + weight * (to[index] - from[index]);
	return result;
}
}
