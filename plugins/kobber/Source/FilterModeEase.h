#pragma once

namespace vekt::mono
{
// The Mode control's easing, shared by both filter types: within each half (LP -> Notch, Notch -> HP) the blend
// follows a smoothstep of the position, so the response eases in and out of each landmark and its slope is zero
// there. position is 0..1 within the half.
[[nodiscard]] inline double filterModeEase(double position) noexcept
{
	return position * position * (3.0 - 2.0 * position);
}
}
