#pragma once

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <concepts>
#include <limits>

namespace vekt::dsp
{
enum class ControlTransitionPolicy
{
	normal,
	artifactSafe,
	operatingPoint
};

template <std::floating_point Sample>
class ControlTransition final
{
public:
	void prepare(double sampleRate, double normalRampSeconds = 0.02,
		double artifactSafeRampSeconds = 0.5,
		double operatingPointRampSeconds = 1.0) noexcept
	{
		sampleRateHz = sampleRate;
		normalSeconds = std::max(0.0, normalRampSeconds);
		artifactSafeSeconds = std::max(normalSeconds, artifactSafeRampSeconds);
		operatingPointSeconds = std::max(artifactSafeSeconds, operatingPointRampSeconds);
		applyRamp();
	}

	void reset() noexcept { value.setCurrentAndTargetValue(value.getTargetValue()); }

	void setPolicy(ControlTransitionPolicy newPolicy) noexcept
	{
		if (policy == newPolicy)
			return;
		policy = newPolicy;
		retarget(value.getTargetValue());
	}

	void setTargetValue(Sample target) noexcept { retarget(target); }
	[[nodiscard]] Sample getCurrentValue() const noexcept { return value.getCurrentValue(); }
	[[nodiscard]] Sample getTargetValue() const noexcept { return value.getTargetValue(); }
	[[nodiscard]] Sample getNextValue() noexcept { return value.getNextValue(); }
	void setCurrentAndTargetValue(Sample target) noexcept { value.setCurrentAndTargetValue(target); }

private:
	void applyRamp() noexcept
	{
		const auto seconds = policy == ControlTransitionPolicy::normal
			? normalSeconds
			: (policy == ControlTransitionPolicy::artifactSafe ? artifactSafeSeconds : operatingPointSeconds);
		value.reset(sampleRateHz, seconds);
	}

	void retarget(Sample target) noexcept
	{
		const auto previousTarget = value.getTargetValue();
		const auto tolerance = std::numeric_limits<Sample>::epsilon()
			* std::max(Sample { 1 }, std::abs(previousTarget));
		if (std::abs(target - previousTarget) <= tolerance)
			return;
		const auto current = value.getCurrentValue();
		applyRamp();
		value.setCurrentAndTargetValue(current);
		value.setTargetValue(target);
	}

	juce::SmoothedValue<Sample> value;
	double sampleRateHz { 48'000.0 };
	double normalSeconds { 0.02 };
	double artifactSafeSeconds { 0.15 };
	double operatingPointSeconds { 1.0 };
	ControlTransitionPolicy policy { ControlTransitionPolicy::normal };
};
}
