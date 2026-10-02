#pragma once

#include "FilterModeEase.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace vekt::mono
{
// Output tap coefficients [u, y1, y2, y3, y4] for the continuous LP -> Notch -> HP pole mix, where u is the
// feedback-solved input to stage 1 (before its tanh) and y1..y4 are the stage outputs. Feedback always comes from
// y4, so Mode only changes what is heard. With H one linearised stage and r = 1 / (1 + k):
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

// Level of the Notch -> HP half against the SVF (ADR 0005, level matching, 2 October 2026). The ladder's linear HP,
// r (1 - H)^4 / (1 + k H^4), keeps the low-pass's passband loss r = 1 / (1 + k), and its zeros cancel much of the
// resonant peak, so at HP it sits 2.5 dB (Resonance 0) to about 10 dB (90 %) below the SVF's high-pass, for every note
// and cutoff alike and at any level. The lift L(k) = 10^(2.5 / 20) (1 + k)^0.6 matches the K-weighted level of the
// filters' linear models; it eases in across the Notch -> HP half like the taps, so Mode at or below Notch is exactly
// the plain pole mix. k is the nominal feedback gain (ladderFeedbackGain), not the level-dependent authority.
inline constexpr double ladderHighPassLiftDecibels = 2.5;
inline constexpr double ladderHighPassLiftPower = 0.6;

[[nodiscard]] inline double ladderHighPassLevel(double mode, double k) noexcept
{
	if (mode <= 0.0) return 1.0;
	const auto lift = std::pow(10.0, ladderHighPassLiftDecibels / 20.0) * std::pow(1.0 + std::max(0.0, k), ladderHighPassLiftPower);
	return 1.0 + filterModeEase(std::min(mode, 1.0)) * (lift - 1.0);
}

// Input and output gains of the Notch -> HP half (ADR 0005, 2 October 2026). The ladder's first stage saturates the raw
// input (tanh, no knee), so a hot, detuned mix intermodulates into dense products a few Hz apart; the low-pass buries
// them under the fundamentals, the steep high-pass exposes them as a jittery, static-like noise. Near the top of
// Resonance the ladder's own resonant gain drives the stages again. The voice therefore lowers the ladder's input at HP
// by 18 dB up to Resonance 80 %, rising to 30 dB at 97.9 % (chosen by ear over a flat 18 dB and a rise to 24 dB), eased in
// from Notch like the taps, and multiplies the output by the inverse, so the linear level is unchanged and only the
// saturation moves. Drive eats the reduction dB for dB, so Drive still takes the high-pass into saturation as before.
// Self-oscillation does not scale with the input, so the make-up must not reach it: the reduction and its make-up fade
// out together over 97.9-98.4 % (k reaches 4 at about 98.4 %; ladderHighPassSelfOscillationFade). At HP the ladder no
// longer gets there (ladderHighPassResonance); the fade covers Modes between Notch and HP. The voice also rescales the
// ladder's state whenever its output gain changes (NonlinearTptLadder::scaleState), so moving Mode or Resonance never
// replays a louder ring or oscillation through the new make-up. Exactly unity at or below Notch.
inline constexpr double ladderHighPassInputReductionDecibels = 18.0, ladderHighPassInputTopReductionDecibels = 30.0;
inline constexpr double ladderHighPassInputRiseStart = 0.8, ladderHighPassInputFadeStart = 0.979, ladderHighPassInputFadeEnd = 0.984;

struct LadderHighPassGains
{
	double input { 1.0 }, output { 1.0 };
};

// 0 below 97.9 %, 1 from 98.4 % (where the ladder can sustain oscillation), smooth between.
[[nodiscard]] inline double ladderHighPassSelfOscillationFade(double resonance) noexcept
{
	return filterModeEase(std::clamp((resonance - ladderHighPassInputFadeStart) / (ladderHighPassInputFadeEnd - ladderHighPassInputFadeStart), 0.0, 1.0));
}

[[nodiscard]] inline LadderHighPassGains ladderHighPassGains(double mode, double resonance, double driveDecibels = 0.0) noexcept
{
	if (mode <= 0.0) return {};
	const auto rise = filterModeEase(std::clamp((resonance - ladderHighPassInputRiseStart)
		/ (ladderHighPassInputFadeStart - ladderHighPassInputRiseStart), 0.0, 1.0));
	const auto reduction = std::max(0.0, ladderHighPassInputReductionDecibels
		+ rise * (ladderHighPassInputTopReductionDecibels - ladderHighPassInputReductionDecibels) - std::max(0.0, driveDecibels));
	const auto decibels = reduction * filterModeEase(std::min(mode, 1.0)) * (1.0 - ladderHighPassSelfOscillationFade(resonance));
	if (decibels <= 0.0) return {};
	const auto input = std::pow(10.0, -decibels / 20.0);
	return { input, 1.0 / input };
}

// The Resonance the ladder's solve gets across Notch -> HP (ADR 0005, 2 October 2026): the high-pass stops just short of
// self-oscillation. Its oscillation interacts with the played note through the saturating stages, which the low-pass's
// four poles hide and the high-pass exposes; no input or output gain law cleaned it (both auditioned). At HP the top of
// the knob is compressed, r for r <= 90 %, 0.9 + 0.1 (u - 0.21 u^2) with u = (r - 0.9) / 0.1 above (slope 1 at 90 %,
// still rising at 100 %), so 100 % is 97.9 % (k = 3.916, below the onset at 4) and the knob keeps working. The
// compression is complete from Mode 0.15, not only at HP: where the ladder could still oscillate, its level lift has to
// fade out, and at a mid Mode that took the level down by several dB between 98 and 100 %. Below Mode 0.15 the lift is
// at most about +1.3 dB. LP and Notch keep the full range and the low-pass its self-oscillation. Exactly r at or below
// Notch.
inline constexpr double ladderHighPassResonanceModeSpan = 0.15;

[[nodiscard]] inline double ladderHighPassResonance(double mode, double resonance) noexcept
{
	if (mode <= 0.0 || resonance <= 0.9) return resonance;
	const auto u = std::min(1.0, (resonance - 0.9) / 0.1);
	const auto compressed = 0.9 + 0.1 * (u - 0.21 * u * u);
	return resonance + filterModeEase(std::min(mode / ladderHighPassResonanceModeSpan, 1.0)) * (compressed - resonance);
}

// The mixed output. The taps are applied to a tanh(u / a) and a tanh(y1..y4 / a), not the raw values: under heavy
// Drive the raw stages lag u for seconds (the ladder integrates tanh, which is flat there), so a raw HP leaks
// bass above its passband and passes the driven input clean. With a = 1 the taps are the tanh values each stage
// integrates, the lag sits where they are flat, so the cancellations hold and the output saturates. Any curve
// keeps the DC cancellation, but a = 1 also compresses a hot mixer by 3-5 dB at no Drive, where raw taps do not
// leak; so the knee widens as Drive falls (ladderPoleMixKnee). k should be the feedback gain scaled by
// ladderFeedbackAuthority. Mode at or below -1 returns y4 untouched, so LP is bit-identical to the plain ladder,
// and the LP -> Notch half fades from that raw y4. inputTanh/stageTanh are tanh(u) and tanh(y), reused when a = 1.
[[nodiscard]] inline double ladderPoleMix(double mode, double k, double feedbackInput, const std::array<double, 4>& stages,
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
