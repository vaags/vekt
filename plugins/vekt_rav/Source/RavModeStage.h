#pragma once

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
	fuzz,
	wavefold,
	bitcrush
};

class RavModeStage final
{
public:
	void prepare(double processingSampleRate, double timingSampleRate = 0.0) noexcept
	{
		sampleRateHz = static_cast<float>(processingSampleRate);
		timingRateHz = static_cast<float>(timingSampleRate > 0.0 ? timingSampleRate : processingSampleRate);
		drive.reset(processingSampleRate, 0.02);
		bias.reset(processingSampleRate, 0.02);
		character.reset(processingSampleRate, 0.02);
		response.reset(processingSampleRate, 0.02);
		texture.reset(processingSampleRate, 0.02);
		reset();
	}

	void reset() noexcept
	{
		previousOutput = 0.0f;
		feedbackState = 0.0f;
		envelope = 0.0f;
		highPassState = 0.0f;
		holdValue = 0.0f;
		holdPhase = 1.0f;
		drive.setCurrentAndTargetValue(drive.getTargetValue());
		bias.setCurrentAndTargetValue(bias.getTargetValue());
		character.setCurrentAndTargetValue(character.getTargetValue());
		response.setCurrentAndTargetValue(response.getTargetValue());
		texture.setCurrentAndTargetValue(texture.getTargetValue());
	}

	void setParameters(RavMode newMode, float newDrive, float newBias,
		float newCharacter, float newResponse, float newTexture) noexcept
	{
		mode = newMode;
		drive.setTargetValue(std::clamp(newDrive, 0.0f, 64.0f));
		bias.setTargetValue(std::clamp(newBias, -1.0f, 1.0f));
		character.setTargetValue(std::clamp(newCharacter, 0.0f, 1.0f));
		response.setTargetValue(std::clamp(newResponse, 0.0f, 1.0f));
		texture.setTargetValue(std::clamp(newTexture, 0.0f, 1.0f));
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
		const auto characterValue = character.getNextValue();
		const auto responseValue = response.getNextValue();
		const auto textureValue = texture.getNextValue();
		const auto driven = input * juce::Decibels::decibelsToGain(driveDb);

		switch (mode)
		{
			case RavMode::saturation:
			{
				const auto rollOff = 0.005f + responseValue * 0.2f;
				feedbackState += (previousOutput - feedbackState) * rollOff;
				const auto memory = characterValue * feedbackState;
				const auto output = std::tanh(driven + memory + biasValue * textureValue)
					- std::tanh(biasValue * textureValue);
				previousOutput = output;
				return output;
			}
			case RavMode::overdrive:
			{
				const auto hpCoefficient = std::clamp(
					0.001f + responseValue * 0.08f, 0.001f, 0.2f);
				const auto highPassed = driven - highPassState;
				highPassState += (driven - highPassState) * hpCoefficient;
				const auto asymmetricBias = biasValue + (characterValue - 0.5f) * 0.8f;
				const auto shaped = std::tanh(highPassed + asymmetricBias)
					- std::tanh(asymmetricBias);
				return std::tanh(shaped * (1.0f + textureValue * 3.0f));
			}
			case RavMode::distortion:
			{
				const auto exponent = 1.2f + characterValue * 6.0f;
				const auto magnitude = std::abs(driven + biasValue * textureValue);
				return std::copysign(magnitude / std::pow(1.0f + std::pow(magnitude, exponent),
					1.0f / exponent), driven + biasValue * textureValue);
			}
			case RavMode::fuzz:
			{
				const auto envelopeRate = 0.001f + responseValue * 0.08f;
				envelope += (std::abs(driven) - envelope) * envelopeRate;
				const auto starvation = biasValue + (0.5f - envelope) * characterValue;
				const auto gated = std::abs(driven) < textureValue * (0.05f + envelope) ? 0.0f : driven;
				return std::clamp((gated + starvation) * 4.0f, -1.0f, 1.0f);
			}
			case RavMode::wavefold:
			{
				const auto folds = 1.0f + characterValue * 7.0f;
				const auto offset = biasValue * responseValue;
				return std::sin((driven + offset) * folds) * (0.6f + textureValue * 0.4f);
			}
			case RavMode::bitcrush:
			{
				const auto bits = 4.0f + characterValue * 12.0f;
				const auto levels = std::pow(2.0f, bits - 1.0f);
				const auto targetRate = 0.02f + responseValue * 0.98f;
				const auto phaseIncrement = targetRate * timingRateHz / sampleRateHz;
				holdPhase += phaseIncrement;
				if (holdPhase >= 1.0f)
				{
					holdPhase -= std::floor(holdPhase);
					holdValue = std::round((driven + biasValue * textureValue) * levels) / levels;
				}
				return holdValue;
			}
		}

		return input;
	}

	RavMode mode { RavMode::saturation };
	float sampleRateHz { 48'000.0f };
	float timingRateHz { 48'000.0f };
	float previousOutput {};
	float feedbackState {};
	float envelope {};
	float highPassState {};
	float holdValue {};
	float holdPhase { 1.0f };
	juce::SmoothedValue<float> drive { 6.0f };
	juce::SmoothedValue<float> bias;
	juce::SmoothedValue<float> character { 0.5f };
	juce::SmoothedValue<float> response { 0.5f };
	juce::SmoothedValue<float> texture { 0.5f };
};
}
