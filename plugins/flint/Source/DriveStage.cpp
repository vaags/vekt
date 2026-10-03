#include "DriveStage.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace vekt::flint
{
namespace
{
constexpr double driveRampSeconds = 0.02;
constexpr double crossfadeSeconds = 0.005;
// Below this input step the antiderivative difference loses precision; use the curve at the midpoint instead.
constexpr double antiAliasingFallbackStep = 1.0e-7;

[[nodiscard]] double antiAliased(DriveType type, double input, double previousInput) noexcept
{
	const auto step = input - previousInput;
	if (std::abs(step) < antiAliasingFallbackStep) return DriveStage::shape(type, 0.5 * (input + previousInput));
	return (DriveStage::antiderivative(type, input) - DriveStage::antiderivative(type, previousInput)) / step;
}

[[nodiscard]] double normalized(DriveType type, double input, double previousInput, double gain) noexcept
{
	return antiAliased(type, input, previousInput) * 0.5 / DriveStage::nominalPeak(type, gain);
}
}

void DriveStage::prepare(double sampleRate) noexcept
{
	driveAmount.reset(sampleRate, driveRampSeconds);
	wetMix.reset(sampleRate, crossfadeSeconds);
	typeMix.reset(sampleRate, crossfadeSeconds);
	reset();
}

void DriveStage::reset() noexcept
{
	driveAmount.setCurrentAndTargetValue(driveAmount.getTargetValue());
	wetMix.setCurrentAndTargetValue(wetMix.getTargetValue());
	typeMix.setCurrentAndTargetValue(1.0);
	previousType = currentType;
	primed = {};
}

void DriveStage::setTarget(double drive, DriveType type) noexcept
{
	const auto driven = drive > 0.0;
	if (isBypassed())
	{
		// Nothing sounds through the curves yet: jump to the target and fade the driven path in.
		driveAmount.setCurrentAndTargetValue(drive);
		currentType = type;
		previousType = type;
		typeMix.setCurrentAndTargetValue(1.0);
	}
	else
	{
		driveAmount.setTargetValue(drive);
		if (type != currentType)
		{
			previousType = currentType;
			currentType = type;
			typeMix.setCurrentAndTargetValue(0.0);
			typeMix.setTargetValue(1.0);
		}
	}
	wetMix.setTargetValue(driven ? 1.0 : 0.0);
}

bool DriveStage::isBypassed() const noexcept { return !wetMix.isSmoothing() && wetMix.getCurrentValue() <= 0.0; }

bool DriveStage::isRinging() const noexcept
{
	// With silent input, only the anti-aliasing memory makes output: every curve maps 0 to 0, so running ramps alone
	// (Drive automated through the end of a tail) do not keep the object awake.
	if (isBypassed()) return false;
	return std::abs(previousScaledInput[0]) > 0.0 || std::abs(previousScaledInput[1]) > 0.0;
}

void DriveStage::process(std::span<float> left, std::span<float> right) noexcept
{
	if (isBypassed()) return;
	const std::array<std::span<float>, 2> channels { left, right };
	for (std::size_t index = 0; index < left.size(); ++index)
	{
		const auto gain = inputGain(driveAmount.getNextValue());
		const auto wet = wetMix.getNextValue();
		const auto curveMix = typeMix.getNextValue();
		for (std::size_t channel = 0; channel < channels.size(); ++channel)
		{
			auto& sample = channels[channel][index];
			const auto dry = static_cast<double>(sample);
			const auto input = gain * dry;
			auto& previous = previousScaledInput[channel];
			if (!primed[channel])
			{
				previous = input;
				primed[channel] = true;
			}
			auto driven = normalized(currentType, input, previous, gain);
			if (curveMix < 1.0)
				driven = normalized(previousType, input, previous, gain) * (1.0 - curveMix) + driven * curveMix;
			previous = input;
			sample = static_cast<float>(dry + wet * (driven - dry));
		}
	}
	if (isBypassed()) primed = {};
}

double DriveStage::inputGain(double drive) noexcept { return std::pow(10.0, 1.8 * drive); }

double DriveStage::shape(DriveType type, double x) noexcept
{
	switch (type)
	{
	case DriveType::soft:
		return std::tanh(x);
	case DriveType::hard:
		return std::clamp(x, -1.0, 1.0);
	case DriveType::fold:
		return std::sin(0.5 * std::numbers::pi * x);
	}
	return x;
}

double DriveStage::antiderivative(DriveType type, double x) noexcept
{
	switch (type)
	{
	case DriveType::soft:
	{
		// log(cosh x), written to stay finite for large |x|.
		const auto magnitude = std::abs(x);
		return magnitude + std::log1p(std::exp(-2.0 * magnitude)) - std::numbers::ln2;
	}
	case DriveType::hard:
	{
		const auto magnitude = std::abs(x);
		return magnitude <= 1.0 ? 0.5 * x * x : magnitude - 0.5;
	}
	case DriveType::fold:
		return -2.0 / std::numbers::pi * std::cos(0.5 * std::numbers::pi * x);
	}
	return 0.5 * x * x;
}

double DriveStage::nominalPeak(DriveType type, double gain) noexcept
{
	const auto reach = 0.5 * gain;
	switch (type)
	{
	case DriveType::soft:
		return std::tanh(reach);
	case DriveType::hard:
		return std::min(reach, 1.0);
	case DriveType::fold:
		return reach < 1.0 ? std::sin(0.5 * std::numbers::pi * reach) : 1.0;
	}
	return reach;
}
}
