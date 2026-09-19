#pragma once

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
		sampleRateHz = static_cast<float>(processingSampleRate);
		reset();
	}

	void reset() noexcept
	{
		inputCouplingState = 0.0f;
		interstageCouplingState = 0.0f;
		biasRecoveryState = 0.0f;
	}

	[[nodiscard]] float process(float driven, float bias, float shape, float dynamics,
		float texture) noexcept
	{
		const auto boundedBias = std::clamp(bias, -1.0f, 1.0f);
		const auto boundedShape = std::clamp(shape, 0.0f, 1.0f);
		const auto boundedDynamics = std::clamp(dynamics, 0.0f, 1.0f);
		const auto boundedTexture = std::clamp(texture, 0.0f, 1.0f);

		const auto inputCoupling = coefficientFromHz(45.0f + boundedDynamics * 95.0f);
		const auto recovery = coefficientFromHz(3.0f + boundedDynamics * 140.0f);
		const auto interstageCoupling = coefficientFromHz(30.0f + boundedTexture * 110.0f);

		// C1: input coupling capacitor. The following base node is AC coupled.
		inputCouplingState += inputCoupling * (driven - inputCouplingState);
		const auto baseInput = driven - inputCouplingState;

		// Cbias: rectified collector current shifts the effective operating point,
		// then recovers at a Dynamics-controlled rate.
		const auto excitation = std::abs(baseInput);
		biasRecoveryState += recovery * (excitation - biasRecoveryState);
		const auto starvation = boundedTexture * (0.20f + 0.80f * boundedShape);
		const auto firstBias = boundedBias * 0.65f + (boundedShape - 0.5f) * 0.45f
			- starvation * (0.35f + 0.65f * biasRecoveryState);
		const auto firstGain = 1.6f + boundedShape * 3.0f;
		const auto firstStage = centredTanh(baseInput * firstGain, firstBias);

		// C2: interstage coupling means the second stage receives the first
		// transistor's changing collector voltage rather than a static waveshape.
		interstageCouplingState += interstageCoupling * (firstStage - interstageCouplingState);
		const auto secondInput = firstStage - interstageCouplingState;
		const auto secondBias = -firstBias * (0.45f + 0.35f * boundedTexture)
			- biasRecoveryState * starvation * 0.35f;
		const auto secondGain = 3.0f + boundedTexture * 6.0f;
		return std::clamp(centredTanh(secondInput * secondGain, secondBias), -1.25f, 1.25f);
	}

private:
	[[nodiscard]] float coefficientFromHz(float frequencyHz) const noexcept
	{
		const auto boundedFrequency = std::clamp(frequencyHz, 1.0f, sampleRateHz * 0.45f);
		return -std::expm1(-2.0f * juce::MathConstants<float>::pi * boundedFrequency / sampleRateHz);
	}

	[[nodiscard]] static float centredTanh(float input, float operatingPoint) noexcept
	{
		return std::tanh(input + operatingPoint) - std::tanh(operatingPoint);
	}

	float sampleRateHz { 48'000.0f };
	float inputCouplingState {};
	float interstageCouplingState {};
	float biasRecoveryState {};
};
}