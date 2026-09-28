#pragma once

#include <algorithm>

namespace vekt::mono
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
}