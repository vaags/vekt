#pragma once

#include "FilterModeEase.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace vekt::mono
{
// Output taps [u, y1, y2, y3, y4] for the continuous LP -> Notch -> HP pole mix, where u is the feedback-solved
// input to stage 1 (before its tanh) and y1..y4 are the stage outputs. Feedback always comes from y4, so Mode
// only changes what is heard. With H one linearised stage and r = 1 / (1 + k):
//   LP    H^4                                   (the plain ladder; its passband is r)
//   Notch r (1 - 2H + 2H^2)(1 + kH)             (zero at the stage pole; both passbands r)
//   HP    r (1 - H)^4                           (high passband r)
// Each half blends its two landmarks by a smoothstep of the position, so the response eases in and out of LP,
// Notch and HP instead of changing at a constant rate right up to them (linearly, the high band leaving LP
// rises like 20 log t: -20 dB at 10% already). The blend is still convex, so the low passband stays r from
// LP to Notch and the high passband stays r from Notch to HP. At DC every stage settles at u, so these
// cancellations survive saturation.
using LadderPoleMixTaps = std::array<double, 5>;

// mode: -1 = LP, 0 = Notch, +1 = HP. k: the feedback gain the ladder actually uses.
[[nodiscard]] inline LadderPoleMixTaps ladderPoleMixTaps(double mode, double k) noexcept
{
	const auto m = std::clamp(mode, -1.0, 1.0);
	const auto r = 1.0 / (1.0 + k);
	constexpr LadderPoleMixTaps lowPass { 0.0, 0.0, 0.0, 0.0, 1.0 };
	const LadderPoleMixTaps notch { r, r * (k - 2.0), r * (2.0 - 2.0 * k), r * 2.0 * k, 0.0 };
	const LadderPoleMixTaps highPass { r, -4.0 * r, 6.0 * r, -4.0 * r, r };
	const auto& from = m <= 0.0 ? lowPass : notch;
	const auto& to = m <= 0.0 ? notch : highPass;
	const auto position = m <= 0.0 ? m + 1.0 : m;
	const auto t = filterModeEase(position);
	LadderPoleMixTaps taps {};
	for (std::size_t tap = 0; tap < taps.size(); ++tap) taps[tap] = (1.0 - t) * from[tap] + t * to[tap];
	return taps;
}

// Mode at or below -1 returns y4 untouched, so the LP position is bit-identical to the plain ladder.
[[nodiscard]] inline double ladderPoleMix(double mode, double k, double feedbackInput,
	const std::array<double, 4>& stages) noexcept
{
	if (mode <= -1.0) return stages[3];
	const auto taps = ladderPoleMixTaps(mode, k);
	auto sum = taps[0] * feedbackInput;
	for (std::size_t stage = 0; stage < stages.size(); ++stage) sum += taps[stage + 1] * stages[stage];
	return sum;
}

// How much of the feedback gain k still acts once the stages saturate, for the peak level E of the driven input.
// The saturated LP stops losing bass to resonance as E rises, so the saturated Notch/HP use k * s in their
// normalisation. Fitted to sine measurements (inputs 0.25-1 at 0-24 dB Drive all collapse onto E = level * D).
[[nodiscard]] inline double ladderFeedbackAuthority(double drivenPeak) noexcept
{
	return 1.0 / (1.0 + std::pow(std::max(0.0, drivenPeak) / 2.45, 2.9));
}

// Knee a of the saturated taps for linear Drive gain D: 3 at 0 dB, 1 from about +19 dB.
[[nodiscard]] inline double ladderPoleMixKnee(double driveGain) noexcept
{
	return std::max(1.0, 3.0 / std::sqrt(driveGain));
}

// A/B variant: the same taps applied to a tanh(u / a) and a tanh(y1..y4 / a). Under heavy Drive the raw stages
// lag u for seconds (the ladder integrates tanh, which is flat there), so the raw HP leaks bass and passes
// the driven input clean. With a = 1 the taps are the tanh values each stage integrates, the lag sits where
// they are flat, so the cancellations hold and the output saturates. Any curve keeps the DC cancellation, but
// a = 1 also compresses a hot mixer by 3-5 dB at no Drive, where the raw taps do not leak; so the knee
// widens as Drive falls (ladderPoleMixKnee). LP stays the raw y4, and the LP -> Notch half fades from it,
// so Mode = LP is unchanged. inputTanh/stageTanh are tanh(u) and tanh(y), reused when a = 1.
[[nodiscard]] inline double ladderPoleMixSaturated(double mode, double k, double feedbackInput, const std::array<double, 4>& stages,
	double knee, double inputTanh, const std::array<double, 4>& stageTanh) noexcept
{
	if (mode <= -1.0) return stages[3];
	auto taps = stageTanh;
	auto input = inputTanh;
	if (knee > 1.0)
	{
		input = knee * std::tanh(feedbackInput / knee);
		for (std::size_t stage = 0; stage < taps.size(); ++stage) taps[stage] = knee * std::tanh(stages[stage] / knee);
	}
	const auto dot = [&](const LadderPoleMixTaps& coefficients)
	{
		auto sum = coefficients[0] * input;
		for (std::size_t stage = 0; stage < taps.size(); ++stage) sum += coefficients[stage + 1] * taps[stage];
		return sum;
	};
	if (mode > 0.0) return dot(ladderPoleMixTaps(mode, k));
	const auto position = mode + 1.0;
	const auto t = filterModeEase(position);
	return (1.0 - t) * stages[3] + t * dot(ladderPoleMixTaps(0.0, k));
}
}
