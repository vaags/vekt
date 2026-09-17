#pragma once

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <concepts>

namespace vekt::dsp
{
enum class ControlTransitionPolicy
{
	normal,
	artifactSafe
};

template <std::floating_point Sample>
class ControlTransition final
{
public:
	void prepare(double sampleRate, double normalRampSeconds = 0.02,
		double artifactSafeRampSeconds = 0.5) noexcept
	{
		sampleRateHz = sampleRate;
		normalSeconds = std::max(0.0, normalRampSeconds);
		artifactSafeSeconds = std::max(normalSeconds, artifactSafeRampSeconds);
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
		value.reset(sampleRateHz, policy == ControlTransitionPolicy::normal
			? normalSeconds : artifactSafeSeconds);
	}

	void retarget(Sample target) noexcept
	{
		if (target == value.getTargetValue())
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
	ControlTransitionPolicy policy { ControlTransitionPolicy::normal };
};
}
