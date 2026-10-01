#pragma once

#include <algorithm>

namespace vekt::mono
{
// The cutoff range a voice actually reaches once keyboard, contour, velocity and LFO offsets are applied. Wider than
// the Cutoff knob (5 Hz to 20 kHz), so modulation can carry the filter past either end of its travel.
inline constexpr float minimumCutoffHz = 2.5f;
inline constexpr float absoluteMaximumCutoffHz = 32'000.0f;

[[nodiscard]] inline float maximumCutoffHz(double sampleRate) noexcept
{
	return std::min(absoluteMaximumCutoffHz, static_cast<float>(sampleRate) * 0.45f);
}
}
