#pragma once

#include <algorithm>
#include <cmath>

namespace vekt::flint
{
// The output's safety clip (docs/FLINT_VALIDATION.md, Shared stages): the identity up to −1 dBFS, then a quadratic knee
// whose slope falls from 1 to 0 as it reaches the 0 dBFS ceiling, and the ceiling beyond.
inline constexpr double safetyClipThreshold = 0.8912509381337456; // −1 dBFS

[[nodiscard]] inline double safetyClip(double x) noexcept
{
	const auto magnitude = std::abs(x);
	if (magnitude <= safetyClipThreshold) return x;
	constexpr auto headroom = 1.0 - safetyClipThreshold;
	const auto over = magnitude - safetyClipThreshold;
	if (over >= 2.0 * headroom) return std::copysign(1.0, x);
	return std::copysign(safetyClipThreshold + over - over * over / (4.0 * headroom), x);
}
}
