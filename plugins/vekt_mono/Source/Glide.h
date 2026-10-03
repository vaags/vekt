#pragma once

#include <cmath>

namespace vekt::mono
{
// Portamento in note space: a one-pole toward the target note with Glide Time as its time constant. Double, so it
// reaches the note at every internal rate (ARCHITECTURE.md, DSP Contracts).
struct Glide
{
	double current {}, target {};

	void jump(float note) noexcept { current = target = note; }
	void retarget(float note) noexcept { target = note; }
	// The note for this sample; snaps to the target when not gliding.
	float next(bool gliding, float seconds, double sampleRate) noexcept
	{
		const auto coefficient = !gliding || seconds <= 0.0f ? 1.0 : 1.0 - std::exp(-1.0 / (static_cast<double>(seconds) * sampleRate));
		current += (target - current) * coefficient;
		return static_cast<float>(current);
	}
};
}
