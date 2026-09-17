#pragma once

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>

namespace vekt::rav
{
class RavPostStage final
{
public:
	void prepare(double sampleRate) noexcept
	{
		sampleRateHz = static_cast<float>(sampleRate);
		reset();
	}

	void reset() noexcept
	{
		previousOutput = 0.0f;
	}

	[[nodiscard]] float process(float input, float cutoffHz) noexcept
	{
		if (cutoffHz <= 0.0f)
		{
			previousOutput = input;
			return input;
		}

		const auto boundedCutoff = std::clamp(cutoffHz, 20.0f, sampleRateHz * 0.45f);
		const auto coefficient = 1.0f - std::exp(
			-2.0f * juce::MathConstants<float>::pi * boundedCutoff / sampleRateHz);
		previousOutput += coefficient * (input - previousOutput);
		return previousOutput;
	}

private:
	float sampleRateHz { 48'000.0f };
	float previousOutput {};
};
}
