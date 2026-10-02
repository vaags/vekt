#pragma once

#include "FilterModeEase.h"
#include "NonlinearTptKorg35.h"

#include <algorithm>
#include <cmath>

namespace vekt::mono
{
// K35 voicing (ADR 0007), accepted after the real-synth audition (30 September 2026).
//
// The knee puts the diode stage at L0 = 0.5 of it at Drive 0 for a mixer level of 1 (a unit-peak saw). A patch with
// several oscillators near full level drives it further (about L = 1.5 with all three), which is part of the sound.
inline constexpr double korg35Knee = 2.0;

// Output trim for Resonance r, (1 + 4 r)^-0.8. Like the untrimmed SVF, K35's passband does not fall with Resonance,
// while the Ladder's does; this law, measured through the real processor, puts K35 within 0.9 dB of the SVF and
// within 3 dB of the Ladder below full Resonance at Drive 0 (ADR 0007). It is the SVF's law today, but K35 owns it:
// a change to the SVF's voicing must not move K35.
inline constexpr double korg35OutputTrimDepth = 0.8;

[[nodiscard]] inline double korg35OutputTrim(double resonance) noexcept
{
	return std::pow(1.0 + 4.0 * std::clamp(resonance, 0.0, 1.0), -korg35OutputTrimDepth);
}

// High-pass side trim (ADR 0007, level matching, 2 October 2026): the MS-20's 6 dB/oct high-pass keeps more of a note's
// low harmonics than the SVF's 12 dB one, about +2 to +4 dB K-weighted at Resonance 0-90 %. A constant -3 dB eases in
// across Mode 0 -> +1 (filterModeEase), so the low-pass and the halfway bell are unchanged.
inline constexpr double korg35HighPassTrimDecibels = -3.0;

[[nodiscard]] inline double korg35HighPassTrim(double mode) noexcept
{
	if (mode <= 0.0) return 1.0;
	return 1.0 + filterModeEase(std::min(mode, 1.0)) * (std::pow(10.0, korg35HighPassTrimDecibels / 20.0) - 1.0);
}

// Loop gain at 100 % Resonance: above the self-oscillation threshold 7/3, so only the top of the knob self-oscillates.
inline constexpr double korg35MaximumFeedback = 2.4;

// Resonance (0..1) to the loop gain rho. Landmarks, pinned exactly: Q 0.5 at 0 %, Q 8 at 80 %, Q 100 at 95 % (Q =
// 1 / (7/3 - rho) below threshold) and rho 2.4 at 100 %. Between them the segments are exponential in Q (0-80 %,
// 80-95 %), then linear in rho from 95 % across the threshold (at about 95.4 %) to the maximum. Each join is rounded
// by a cubic Hermite blend (in log(1/Q) around 80 %, in rho around 95 %) whose slope at the knot is the harmonic mean
// of its neighbours' slopes: the map is monotonic and C1, and the landmarks stay where they were.
namespace korg35_detail
{
inline constexpr double firstKnot = 0.8, secondKnot = 0.95, firstBlend = 0.04, secondBlend = 0.02;
inline constexpr double edgeDamping = 0.01; // 1 / Q at 95 %

// log(1/Q) of the unsmoothed exponential segments and its slope in r.
[[nodiscard]] inline double logDamping(double r) noexcept
{
	return r <= firstKnot ? std::log(2.0) - std::log(16.0) / firstKnot * r
		: std::log(1.0 / 8.0) - std::log(12.5) / (secondKnot - firstKnot) * (r - firstKnot);
}

[[nodiscard]] inline double logDampingSlope(double r) noexcept
{
	return r <= firstKnot ? -std::log(16.0) / firstKnot : -std::log(12.5) / (secondKnot - firstKnot);
}

[[nodiscard]] inline double hermite(double x, double x0, double x1, double y0, double y1, double d0, double d1) noexcept
{
	const auto h = x1 - x0, t = (x - x0) / h, t2 = t * t, t3 = t2 * t;
	return (2.0 * t3 - 3.0 * t2 + 1.0) * y0 + (t3 - 2.0 * t2 + t) * h * d0 + (-2.0 * t3 + 3.0 * t2) * y1 + (t3 - t2) * h * d1;
}

[[nodiscard]] inline double harmonicMean(double a, double b) noexcept { return 2.0 * a * b / (a + b); }

[[nodiscard]] inline double linearTop(double r) noexcept
{
	const auto edge = nonlinearTptKorg35Threshold - edgeDamping;
	return edge + (korg35MaximumFeedback - edge) * (r - secondKnot) / (1.0 - secondKnot);
}
}

[[nodiscard]] inline double korg35Feedback(double resonance) noexcept
{
	using namespace korg35_detail;
	const auto r = std::clamp(resonance, 0.0, 1.0);
	const auto threshold = nonlinearTptKorg35Threshold;
	// Around 95 %, in rho: from the exponential segment into the linear top. On the exponential segment rho =
	// 7/3 - e^L, so d rho / dr = -e^L L' (positive, as L' < 0).
	if (std::abs(r - secondKnot) < secondBlend)
	{
		const auto left = secondKnot - secondBlend, right = secondKnot + secondBlend;
		const auto slopeAt = [](double x) { return -std::exp(logDamping(x)) * logDampingSlope(x); };
		const auto topSlope = (korg35MaximumFeedback - (threshold - edgeDamping)) / (1.0 - secondKnot);
		const auto knotSlope = harmonicMean(slopeAt(secondKnot), topSlope);
		const auto knot = threshold - edgeDamping;
		return r <= secondKnot ? hermite(r, left, secondKnot, threshold - std::exp(logDamping(left)), knot, slopeAt(left), knotSlope)
							   : hermite(r, secondKnot, right, knot, linearTop(right), knotSlope, topSlope);
	}
	if (r > secondKnot) return linearTop(r);
	// Around 80 %, in log(1/Q).
	if (std::abs(r - firstKnot) < firstBlend)
	{
		const auto left = firstKnot - firstBlend, right = firstKnot + firstBlend;
		const auto knotSlope = -harmonicMean(-logDampingSlope(left), -logDampingSlope(right));
		const auto logQ = r <= firstKnot
			? hermite(r, left, firstKnot, logDamping(left), logDamping(firstKnot), logDampingSlope(left), knotSlope)
			: hermite(r, firstKnot, right, logDamping(firstKnot), logDamping(right), knotSlope, logDampingSlope(right));
		return threshold - std::exp(logQ);
	}
	return threshold - std::exp(logDamping(r));
}
}
