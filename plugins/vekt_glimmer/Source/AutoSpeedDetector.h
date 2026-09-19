#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace vekt::glimmer
{
class AutoSpeedDetector final
{
public:
	void prepare(double newSampleRate) noexcept
	{
		sampleRate = std::max(newSampleRate, 1.0);
		attackCoefficient = coefficient(0.01);
		releaseCoefficient = coefficient(0.25);
		minimumDwellSamples = std::max<std::int64_t>(1,
			static_cast<std::int64_t>(std::llround(sampleRate * 0.2)));
		reset();
	}

	void reset() noexcept
	{
		envelope = 0.0f;
		fast = false;
		dwellSamplesRemaining = 0;
	}

	void setSensitivity(float newSensitivity) noexcept
	{
		sensitivity = std::clamp(newSensitivity, 0.0f, 1.0f);
	}

	[[nodiscard]] bool advance(float left, float right) noexcept
	{
		const auto input = std::max(std::abs(left), std::abs(right));
		const auto envelopeCoefficient = input > envelope ? attackCoefficient : releaseCoefficient;
		envelope = input + envelopeCoefficient * (envelope - input);

		if (dwellSamplesRemaining > 0)
		{
			--dwellSamplesRemaining;
			return fast;
		}

		const auto fastThreshold = thresholdLinear();
		const auto slowThreshold = fastThreshold * hysteresisLinear;
		if (!fast && envelope >= fastThreshold)
			switchTarget(true);
		else if (fast && envelope <= slowThreshold)
			switchTarget(false);
		return fast;
	}

	[[nodiscard]] bool isFast() const noexcept { return fast; }
	[[nodiscard]] float getEnvelope() const noexcept { return envelope; }
	[[nodiscard]] float getThresholdDb() const noexcept
	{
		return -6.0f - sensitivity * 42.0f;
	}

private:
	[[nodiscard]] float coefficient(double seconds) const noexcept
	{
		return static_cast<float>(std::exp(-1.0 / (seconds * sampleRate)));
	}

	[[nodiscard]] float thresholdLinear() const noexcept
	{
		return std::pow(10.0f, getThresholdDb() / 20.0f);
	}

	void switchTarget(bool newFast) noexcept
	{
		fast = newFast;
		dwellSamplesRemaining = minimumDwellSamples;
	}

	inline static constexpr float hysteresisLinear { 0.5011872336272722f };
	double sampleRate { 48'000.0 };
	float sensitivity { 0.5f };
	float envelope {};
	float attackCoefficient {};
	float releaseCoefficient {};
	std::int64_t minimumDwellSamples {};
	std::int64_t dwellSamplesRemaining {};
	bool fast {};
};
}
