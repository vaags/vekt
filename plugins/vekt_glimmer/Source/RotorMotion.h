#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace vekt::glimmer
{
enum class RotarySpeedMode
{
	slow,
	fast,
	autoMode
};

class RotorMotion final
{
public:
	void prepare(double newSampleRate, float initialRpm) noexcept
	{
		sampleRate = std::max(newSampleRate, 1.0);
		reset(initialRpm);
	}

	void reset(float initialRpm) noexcept
	{
		currentRpm = clampRpm(initialRpm);
		targetRpm = currentRpm;
		rampStep = 0.0f;
		rampSamplesRemaining = 0;
		phaseTurns = 0.0f;
	}

	void setSpeeds(float newSlowRpm, float newFastRpm) noexcept
	{
		slowRpm = clampRpm(newSlowRpm);
		fastRpm = std::max(slowRpm, clampRpm(newFastRpm));
		retarget();
	}

	void setTransitionTimes(float newAccelerationSeconds,
		float newDecelerationSeconds) noexcept
	{
		accelerationSeconds = std::max(newAccelerationSeconds, 0.001f);
		decelerationSeconds = std::max(newDecelerationSeconds, 0.001f);
		retarget();
	}

	void setMode(RotarySpeedMode newMode) noexcept
	{
		mode = newMode;
		retarget();
	}

	void setAutoFast(bool shouldRunFast) noexcept
	{
		if (autoFast == shouldRunFast)
			return;
		autoFast = shouldRunFast;
		if (mode == RotarySpeedMode::autoMode)
			retarget();
	}

	[[nodiscard]] float advance() noexcept
	{
		if (rampSamplesRemaining > 0)
		{
			currentRpm += rampStep;
			--rampSamplesRemaining;
			if (rampSamplesRemaining == 0)
				currentRpm = targetRpm;
		}

		phaseTurns = wrapTurns(phaseTurns + currentRpm / static_cast<float>(60.0 * sampleRate));
		return phaseTurns;
	}

	[[nodiscard]] float getPhaseTurns() const noexcept { return phaseTurns; }
	[[nodiscard]] float getCurrentRpm() const noexcept { return currentRpm; }
	[[nodiscard]] float getTargetRpm() const noexcept { return targetRpm; }
	[[nodiscard]] RotarySpeedMode getMode() const noexcept { return mode; }

private:
	static constexpr float minimumRpm = 0.0f;
	static constexpr float maximumRpm = 1'200.0f;

	[[nodiscard]] static float clampRpm(float rpm) noexcept
	{
		return std::clamp(rpm, minimumRpm, maximumRpm);
	}

	[[nodiscard]] static float wrapTurns(float turns) noexcept
	{
		return turns - std::floor(turns);
	}

	[[nodiscard]] float requestedRpm() const noexcept
	{
		if (mode == RotarySpeedMode::slow)
			return slowRpm;
		if (mode == RotarySpeedMode::fast)
			return fastRpm;
		return autoFast ? fastRpm : slowRpm;
	}

	void retarget() noexcept
	{
		const auto newTarget = requestedRpm();
		if (std::abs(newTarget - targetRpm) <= 0.0001f)
			return;

		targetRpm = newTarget;
		const auto transitionSeconds = targetRpm > currentRpm
			? accelerationSeconds : decelerationSeconds;
		rampSamplesRemaining = std::max<std::int64_t>(1,
			static_cast<std::int64_t>(std::llround(sampleRate * transitionSeconds)));
		rampStep = (targetRpm - currentRpm)
			/ static_cast<float>(rampSamplesRemaining);
	}

	double sampleRate { 48'000.0 };
	float slowRpm { 48.0f };
	float fastRpm { 400.0f };
	float accelerationSeconds { 2.0f };
	float decelerationSeconds { 4.0f };
	float currentRpm { slowRpm };
	float targetRpm { slowRpm };
	float rampStep {};
	float phaseTurns {};
	std::int64_t rampSamplesRemaining {};
	RotarySpeedMode mode { RotarySpeedMode::slow };
	bool autoFast {};
};
}
