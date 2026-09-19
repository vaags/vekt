#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <optional>

namespace vekt::ui
{
class StereoMeterBallistics final
{
public:
	using Clock = std::chrono::steady_clock;
	using Levels = std::array<float, 2>;

	static constexpr float silenceFloor = 0.0001f;

	const Levels& update(Levels peaks, Clock::time_point timestamp) noexcept
	{
		const auto elapsedSeconds = previousUpdate
			? std::chrono::duration<float>(timestamp - *previousUpdate).count()
			: 0.0f;
		const auto release = std::exp(-releasePerSecond * std::max(0.0f, elapsedSeconds));
		for (std::size_t channel = 0; channel < displayedLevels.size(); ++channel)
		{
			const auto peak = sanitise(peaks[channel]);
			displayedLevels[channel] = std::max(displayedLevels[channel] * release, peak);
			if (displayedLevels[channel] < silenceFloor)
				displayedLevels[channel] = 0.0f;
		}
		previousUpdate = timestamp;
		return displayedLevels;
	}

	const Levels& getDisplayedLevels() const noexcept { return displayedLevels; }

private:
	static constexpr float releasePerSecond = 3.83504f; // Equivalent to RAV's 0.88 decay at 30 Hz.

	static float sanitise(float peak) noexcept
	{
		return std::isfinite(peak) ? std::max(0.0f, peak) : 0.0f;
	}

	Levels displayedLevels {};
	std::optional<Clock::time_point> previousUpdate;
};
}
