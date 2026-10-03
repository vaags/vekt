#pragma once

#include <algorithm>

namespace vekt::kobber
{
// Keep the established sub-98% response; the top of the control enters the
// self-oscillating region rather than stopping at the small-signal boundary.
template <typename T>
[[nodiscard]] inline T ladderFeedbackGain(T resonance) noexcept
{
	const auto r = std::clamp(resonance, T(0), T(1));
	const auto t = std::clamp((r - T(0.98)) / T(0.02), T(0), T(1));
	return T(4) * r + T(0.6) * t * t * (T(3) - T(2) * t);
}

// Finite-amplitude oscillation runs below the small-signal pole frequency.
// Calibrate only the above-onset part of the control; retain sub-onset tuning.
template <typename T>
[[nodiscard]] inline T ladderResonanceTuning(T resonance) noexcept
{
	const auto t = std::clamp((std::clamp(resonance, T(0), T(1)) - T(0.98)) / T(0.02), T(0), T(1));
	return T(1) + T(0.0287) * t * t * (T(3) - T(2) * t);
}
}