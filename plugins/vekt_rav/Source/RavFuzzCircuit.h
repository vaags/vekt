#pragma once

#include "RavCachedCoefficient.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>

namespace vekt::rav
{
// A fixed-cost, transistor-inspired fuzz topology for development comparison.
// It is not a component-identical emulation of a named hardware pedal.
class RavFuzzCircuit final
{
public:
	void prepare(double processingSampleRate) noexcept
	{
		sampleRateHz = processingSampleRate;
		for (auto* coefficient : { &inputCoupling, &recovery, &interstageCoupling }) coefficient->invalidate();
		reset();
	}

	void reset() noexcept
	{
		inputCouplingState = 0.0;
		interstageCouplingState = 0.0;
		biasRecoveryState = 0.0;
	}

	[[nodiscard]] float process(float driven, float bias, float shape, float dynamics,
		float texture) noexcept
	{
		const auto boundedBias = std::clamp(bias, -1.0f, 1.0f);
		const auto boundedShape = std::clamp(shape, 0.0f, 1.0f);
		const auto boundedDynamics = std::clamp(dynamics, 0.0f, 1.0f);
		const auto boundedTexture = std::clamp(texture, 0.0f, 1.0f);

		const auto inputCouplingRate = inputCoupling.get(boundedDynamics, [&] { return coefficientFromHz(45.0f + boundedDynamics * 95.0f); });
		const auto recoveryRate = recovery.get(boundedDynamics, [&] { return coefficientFromHz(3.0f + boundedDynamics * 140.0f); });
		const auto interstageRate = interstageCoupling.get(boundedTexture, [&] { return coefficientFromHz(30.0f + boundedTexture * 110.0f); });

		// C1: input coupling capacitor. The following base node is AC coupled.
		inputCouplingState += inputCouplingRate * (driven - inputCouplingState);
		const auto baseInput = static_cast<float>(driven - inputCouplingState);

		// Cbias: rectified collector current shifts the effective operating point,
		// then recovers at a Dynamics-controlled rate.
		const auto excitation = std::abs(baseInput);
		biasRecoveryState += recoveryRate * (excitation - biasRecoveryState);
		const auto recoveryLevel = static_cast<float>(biasRecoveryState);
		const auto starvation = boundedTexture * (0.20f + 0.80f * boundedShape);
		const auto firstBias = boundedBias * 0.65f + (boundedShape - 0.5f) * 0.45f
			- starvation * (0.35f + 0.65f * recoveryLevel);
		const auto firstGain = 1.6f + boundedShape * 3.0f;
		const auto firstStage = centredTanh(baseInput * firstGain, firstBias);

		// C2: interstage coupling means the second stage receives the first
		// transistor's changing collector voltage rather than a static waveshape.
		interstageCouplingState += interstageRate * (firstStage - interstageCouplingState);
		const auto secondInput = static_cast<float>(firstStage - interstageCouplingState);
		const auto secondBias = -firstBias * (0.45f + 0.35f * boundedTexture)
			- recoveryLevel * starvation * 0.35f;
		const auto secondGain = 3.0f + boundedTexture * 6.0f;
		return std::clamp(centredTanh(secondInput * secondGain, secondBias), -1.25f, 1.25f);
	}

private:
	[[nodiscard]] double coefficientFromHz(float frequencyHz) const noexcept
	{
		const auto boundedFrequency = std::clamp(static_cast<double>(frequencyHz), 1.0, sampleRateHz * 0.45);
		return -std::expm1(-2.0 * juce::MathConstants<double>::pi * boundedFrequency / sampleRateHz);
	}

	[[nodiscard]] static float centredTanh(float input, float operatingPoint) noexcept
	{
		return std::tanh(input + operatingPoint) - std::tanh(operatingPoint);
	}

	// Double: these one-poles reach down to 3 Hz, where float state is measurably off at the highest internal rate
	// (ARCHITECTURE.md, DSP Contracts).
	double sampleRateHz { 48'000.0 };
	double inputCouplingState {};
	double interstageCouplingState {};
	double biasRecoveryState {};
	RavCachedCoefficient inputCoupling, recovery, interstageCoupling;
};
}