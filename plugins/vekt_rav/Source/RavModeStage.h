#pragma once

#include "RavFuzzCircuit.h"
#include "RavPostStage.h"

#include <vekt/dsp/ControlTransition.h>

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <cmath>
#include <concepts>
#include <span>

namespace vekt::rav
{
enum class RavMode
{
	saturation,
	overdrive,
	distortion,
	fuzz
};

// This is intentionally not an APVTS parameter. It provides a development-only
// comparison seam for candidate algorithms without changing saved Rav sessions.
enum class RavProcessingModel
{
	legacy,
	behavioralCandidate,
	overdriveCircuitCandidate,
	fuzzCircuitCandidate
};

class RavModeStage final
{
public:
	void setProcessingModel(RavProcessingModel newModel) noexcept
	{
		if (processingModel == newModel)
			return;
		processingModel = newModel;
		fuzzCircuit.reset();
	}
	[[nodiscard]] RavProcessingModel getProcessingModel() const noexcept { return processingModel; }

	void prepare(double processingSampleRate) noexcept
	{
		sampleRateHz = static_cast<float>(processingSampleRate);
		stateRateScale = referenceProcessingRateHz / sampleRateHz;
		fuzzToneCoefficient = timeCorrectedCoefficient(0.08f);
		drive.prepare(processingSampleRate, 0.02, 0.15);
		bias.prepare(processingSampleRate, 0.02, 0.15);
		shape.prepare(processingSampleRate, 0.02, 0.15);
		dynamics.prepare(processingSampleRate, 0.02, 0.15);
		texture.prepare(processingSampleRate, 0.02, 0.15);
		tone.prepare(processingSampleRate, 0.02, 0.15);
		postStage.prepare(processingSampleRate);
		fuzzCircuit.prepare(processingSampleRate);
		reset();
	}

	void reset() noexcept
	{
		previousOutput = 0.0f;
		feedbackState = 0.0f;
		envelope = 0.0f;
		highPassState = 0.0f;
		fuzzToneState = 0.0f;
		postStage.reset();
		fuzzCircuit.reset();
		drive.setCurrentAndTargetValue(drive.getTargetValue());
		bias.setCurrentAndTargetValue(bias.getTargetValue());
		shape.setCurrentAndTargetValue(shape.getTargetValue());
		dynamics.setCurrentAndTargetValue(dynamics.getTargetValue());
		texture.setCurrentAndTargetValue(texture.getTargetValue());
		tone.setCurrentAndTargetValue(tone.getTargetValue());
	}

	void setParameters(RavMode newMode, float newDrive, float newBias,
		float newShape, float newDynamics, float newTexture, float newTone = 0.0f) noexcept
	{
		mode = newMode;
		drive.setTargetValue(std::clamp(newDrive, 0.0f, 64.0f));
		bias.setTargetValue(std::clamp(newBias, -1.0f, 1.0f));
		shape.setTargetValue(std::clamp(newShape, 0.0f, 1.0f));
		dynamics.setTargetValue(std::clamp(newDynamics, 0.0f, 1.0f));
		texture.setTargetValue(std::clamp(newTexture, 0.0f, 1.0f));
		tone.setTargetValue(std::clamp(newTone, -6.0f, 6.0f));
	}

	void setArtifactSafePolicy(bool enabled) noexcept
	{
		const auto policy = enabled ? dsp::ControlTransitionPolicy::artifactSafe
			: dsp::ControlTransitionPolicy::normal;
		drive.setPolicy(policy);
		bias.setPolicy(enabled ? dsp::ControlTransitionPolicy::operatingPoint : policy);
		shape.setPolicy(policy);
		dynamics.setPolicy(policy);
		texture.setPolicy(policy);
		tone.setPolicy(policy);
	}

	void process(std::span<float> samples) noexcept
	{
		for (auto& sample : samples)
			sample = processSample(sample);
	}

private:
	[[nodiscard]] float processSample(float input) noexcept
	{
		const auto driveDb = drive.getNextValue();
		const auto biasValue = bias.getNextValue();
		const auto shapeValue = shape.getNextValue();
		const auto dynamicsValue = dynamics.getNextValue();
		const auto textureValue = texture.getNextValue();
		const auto toneValue = tone.getNextValue();
		const auto driven = input * juce::Decibels::decibelsToGain(driveDb);

		auto output = input;
		switch (mode)
		{
			case RavMode::saturation:
			{
				const auto rollOff = timeCorrectedCoefficient(0.005f + dynamicsValue * 0.2f);
				feedbackState += (previousOutput - feedbackState) * rollOff;
				const auto memory = shapeValue * feedbackState;
				output = std::tanh(driven + memory + biasValue * textureValue)
					- std::tanh(biasValue * textureValue);
				previousOutput = output;
				break;
			}
			case RavMode::overdrive:
			{
				const auto cutoffHz = 80.0f + dynamicsValue * 40.0f;
				const auto hpCoefficient = 1.0f - std::exp(
					-2.0f * juce::MathConstants<float>::pi * cutoffHz / sampleRateHz);
				const auto highPassed = driven - highPassState;
				highPassState += (driven - highPassState) * hpCoefficient;
				const auto asymmetricBias = biasValue + (shapeValue - 0.5f) * 0.8f;
				const auto shaped = std::tanh(highPassed + asymmetricBias)
					- std::tanh(asymmetricBias);
				output = std::tanh(shaped * (1.0f + textureValue * 3.0f));
				break;
			}
			case RavMode::distortion:
			{
				const auto exponent = 1.2f + shapeValue * 6.0f;
				const auto magnitude = std::abs(driven + biasValue * textureValue);
				output = std::copysign(magnitude / std::pow(1.0f + std::pow(magnitude, exponent),
					1.0f / exponent), driven + biasValue * textureValue);
				break;
			}
			case RavMode::fuzz:
			{
				if (processingModel == RavProcessingModel::fuzzCircuitCandidate)
					output = fuzzCircuit.process(driven, biasValue, shapeValue, dynamicsValue, textureValue);
				else
				{
					const auto envelopeRate = timeCorrectedCoefficient(0.001f + dynamicsValue * 0.08f);
					envelope += (std::abs(driven) - envelope) * envelopeRate;
					const auto starvation = biasValue + (0.5f - envelope) * shapeValue;
					const auto threshold = textureValue * (0.05f + envelope);
					const auto transitionWidth = 0.01f + textureValue * 0.08f;
					const auto gatePosition = std::clamp(
						(std::abs(driven) - threshold + transitionWidth) /
						(2.0f * transitionWidth), 0.0f, 1.0f);
					const auto smoothGate = gatePosition * gatePosition
						* (3.0f - 2.0f * gatePosition);
					const auto gated = driven * smoothGate;
					const auto clipInput = (gated + starvation) * 4.0f;
					output = std::clamp(clipInput, -1.0f, 1.0f);
				}
				break;
			}
		}

		if (mode == RavMode::fuzz)
		{
			const auto tilt = std::clamp(toneValue / 6.0f, -0.8f, 0.8f);
			fuzzToneState += fuzzToneCoefficient * (output - fuzzToneState);
			output += tilt * (output - fuzzToneState);
		}
		return postStage.process(output, postCutoffHz(mode, textureValue));
	}

	[[nodiscard]] float timeCorrectedCoefficient(float referenceCoefficient) const noexcept
	{
		if (stateRateScale == 1.0f)
			return referenceCoefficient;
		return -std::expm1(std::log1p(-referenceCoefficient) * stateRateScale);
	}

	[[nodiscard]] static float postCutoffHz(RavMode currentMode, float textureValue) noexcept
	{
		switch (currentMode)
		{
			case RavMode::saturation: return 10'000.0f + (1.0f - textureValue) * 8'000.0f;
			case RavMode::overdrive: return 11'000.0f + textureValue * 7'000.0f;
			case RavMode::distortion: return 5'000.0f + textureValue * 8'000.0f;
			case RavMode::fuzz: return 4'000.0f + textureValue * 6'000.0f;
		}

		return 0.0f;
	}

	RavMode mode { RavMode::saturation };
	RavProcessingModel processingModel { RavProcessingModel::legacy };
	inline static constexpr float referenceProcessingRateHz { 192'000.0f };
	float sampleRateHz { 48'000.0f };
	float stateRateScale { referenceProcessingRateHz / sampleRateHz };
	float fuzzToneCoefficient { timeCorrectedCoefficient(0.08f) };
	float previousOutput {};
	float feedbackState {};
	float envelope {};
	float highPassState {};
	float fuzzToneState {};
	RavFuzzCircuit fuzzCircuit;
	RavPostStage postStage;
	dsp::ControlTransition<float> drive;
	dsp::ControlTransition<float> bias;
	dsp::ControlTransition<float> shape;
	dsp::ControlTransition<float> dynamics;
	dsp::ControlTransition<float> texture;
	dsp::ControlTransition<float> tone;
};
}
