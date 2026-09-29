#pragma once

#include "FilterModeEase.h"

#include <algorithm>
#include <cmath>

namespace vekt::mono
{
// The SVF's Mode mix of its native outputs (ADR 0006), with the same easing as the ladder's pole mix:
//   -1 .. 0   y = LP + e HP          (-1 LP, 0 Notch = LP + HP)
//    0 .. +1  y = (1 - e) LP + HP    (+1 HP)
// e = filterModeEase of the position within the half. The landmarks are exact, and both halves meet at LP + HP,
// where the slope is zero from either side.
[[nodiscard]] inline double svfModeMix(double mode, double lowPass, double highPass) noexcept
{
	const auto m = std::clamp(mode, -1.0, 1.0);
	if (m <= -1.0) return lowPass;
	if (m >= 1.0) return highPass;
	if (m <= 0.0) return lowPass + filterModeEase(m + 1.0) * highPass;
	return (1.0 - filterModeEase(m)) * lowPass + highPass;
}

// The SVF's knee a: the input saturates as a tanh(v / a) and the damping law is scaled by it. Halving it equals
// 6 dB more Drive and 6 dB less output. 3 balances a clean Drive 0 (about 1 % LP THD at mixer level 1; 4 gave 0.6 %,
// 2 about 2 %) against how far the driven SVF's level runs from the Ladder's (worst +6.7 dB at +12 dB Drive; 4 gave
// +7.5). Locked (ADR 0006): 3 and 4 sound alike, so 3 for the closer switching level.
inline constexpr double svfKnee = 3.0;

// beta in the SVF's damping law psi(B) = B + beta B^3 / a^2: how fast damping grows with amplitude, compressing
// resonance as the filter works harder. Voicing, not a control; locked by listening (ADR 0006): 0.25-1 sound alike,
// and 0.5 takes Q 8's prominence from 14 dB at Drive 0 to about 7 dB at +24 dB.
inline constexpr double svfDampingCurve = 0.5;

// Shape p of the Resonance control, k = 2 - 1.875 r^p: 1 is linear in k; lower p brings resonance in earlier
// while keeping both ends. Locked by listening at 0.5 (ADR 0006): Q 1.5 at 50 %, 2.7 at 75 %, 4.5 at 90 %, where
// linear k left nearly all of the resonance to the top 10-15 % of the knob.
inline constexpr double svfResonanceShape = 0.5;

// Output trim for Resonance r, (1 + 4 r)^-0.8, so switching between Ladder and SVF keeps a sensible level
// (ADR 0006). The ladder's feedback costs its passband 1 / (1 + 4 r) (k = 4 r), about 15 dB at full Resonance, while
// the SVF keeps its passband; the trim follows 80 % of that loss in dB, leaving the SVF slightly louder and more
// open. At Drive 0 the switching level is then within 3 dB on 97 % of the measured fixtures and under 6 dB on all.
inline constexpr double svfOutputTrimDepth = 0.8;

[[nodiscard]] inline double svfOutputTrim(double resonance) noexcept
{
	return std::pow(1.0 + 4.0 * std::clamp(resonance, 0.0, 1.0), -svfOutputTrimDepth);
}

// Resonance 0..1 to SVF damping k = 1/Q, from 2 (Q 0.5) to 0.125 (Q 8). Q stops at 8 for the instrument's sake:
// strongly resonant, short of the Ladder's self-oscillation.
[[nodiscard]] inline double svfDamping(double resonance, double shape = svfResonanceShape) noexcept
{
	const auto r = std::clamp(resonance, 0.0, 1.0);
	return 2.0 - 1.875 * (shape == 1.0 ? r : std::pow(r, shape));
}
}
