#pragma once

#include "RavPostStage.h"

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
		bias.reset(processingSampleRate, biasRampDuration);
		character.reset(processingSampleRate, 0.02);
		response.reset(processingSampleRate, 0.02);
		texture.reset(processingSampleRate, textureRampDuration);
		tone.reset(processingSampleRate, 0.02);
		postStage.prepare(processingSampleRate);
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
		fuzzToneState = 0.0f;
		postStage.reset();
		drive.setCurrentAndTargetValue(drive.getTargetValue());
		bias.setCurrentAndTargetValue(bias.getTargetValue());
		character.setCurrentAndTargetValue(character.getTargetValue());
		response.setCurrentAndTargetValue(response.getTargetValue());
		texture.setCurrentAndTargetValue(texture.getTargetValue());
		tone.setCurrentAndTargetValue(tone.getTargetValue());
	}

	void setParameters(RavMode newMode, float newDrive, float newBias,
		float newCharacter, float newResponse, float newTexture, float newTone = 0.0f) noexcept
	{
		mode = newMode;
		drive.setTargetValue(std::clamp(newDrive, 0.0f, 64.0f));
		bias.setTargetValue(std::clamp(newBias, -1.0f, 1.0f));
		character.setTargetValue(std::clamp(newCharacter, 0.0f, 1.0f));
		response.setTargetValue(std::clamp(newResponse, 0.0f, 1.0f));
		texture.setTargetValue(std::clamp(newTexture, 0.0f, 1.0f));
		tone.setTargetValue(std::clamp(newTone, -6.0f, 6.0f));
	}

	void setBiasRampDurationSeconds(double seconds) noexcept
	{
		if (std::abs(seconds - biasRampDuration) < 1.0e-9)
			return;

		const auto current = bias.getCurrentValue();
		const auto target = bias.getTargetValue();
		bias.reset(sampleRateHz, seconds);
		bias.setCurrentAndTargetValue(current);
		bias.setTargetValue(target);
		biasRampDuration = seconds;
	}

	void setControlRampDurationSeconds(double seconds) noexcept
	{
		setRamp(drive, seconds);
		setRamp(character, seconds);
		setRamp(response, seconds);
		setRamp(bias, biasRampDuration);
		setRamp(texture, textureRampDuration);
	}

	void setTextureRampDurationSeconds(double seconds) noexcept
	{
		if (std::abs(seconds - textureRampDuration) < 1.0e-9)
			return;

		const auto current = texture.getCurrentValue();
		const auto target = texture.getTargetValue();
		texture.reset(sampleRateHz, seconds);
		texture.setCurrentAndTargetValue(current);
		texture.setTargetValue(target);
		textureRampDuration = seconds;
	}

	void process(std::span<float> samples) noexcept
	{
		for (auto& sample : samples)
			sample = processSample(sample);
	}

private:
	template <typename Smoothed>
	void setRamp(Smoothed& smoother, double seconds) noexcept
	{
		const auto current = smoother.getCurrentValue();
		const auto target = smoother.getTargetValue();
		smoother.reset(sampleRateHz, seconds);
		smoother.setCurrentAndTargetValue(current);
		smoother.setTargetValue(target);
	}

	[[nodiscard]] float processSample(float input) noexcept
	{
		const auto driveDb = drive.getNextValue();
		const auto biasValue = bias.getNextValue();
		const auto characterValue = character.getNextValue();
		const auto responseValue = response.getNextValue();
		const auto textureValue = texture.getNextValue();
		const auto toneValue = tone.getNextValue();
		const auto driven = input * juce::Decibels::decibelsToGain(driveDb);

		auto output = input;
		switch (mode)
		{
			case RavMode::saturation:
			{
				const auto rollOff = 0.005f + responseValue * 0.2f;
				feedbackState += (previousOutput - feedbackState) * rollOff;
				const auto memory = characterValue * feedbackState;
				output = std::tanh(driven + memory + biasValue * textureValue)
					- std::tanh(biasValue * textureValue);
				previousOutput = output;
				break;
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
				output = std::tanh(shaped * (1.0f + textureValue * 3.0f));
				break;
			}
			case RavMode::distortion:
			{
				const auto exponent = 1.2f + characterValue * 6.0f;
				const auto magnitude = std::abs(driven + biasValue * textureValue);
				output = std::copysign(magnitude / std::pow(1.0f + std::pow(magnitude, exponent),
					1.0f / exponent), driven + biasValue * textureValue);
				break;
			}
			case RavMode::fuzz:
			{
				const auto envelopeRate = 0.001f + responseValue * 0.08f;
				envelope += (std::abs(driven) - envelope) * envelopeRate;
				const auto starvation = biasValue + (0.5f - envelope) * characterValue;
				const auto threshold = textureValue * (0.05f + envelope);
				const auto transitionWidth = 0.01f + textureValue * 0.08f;
				const auto gatePosition = std::clamp(
					(std::abs(driven) - threshold + transitionWidth) /
					(2.0f * transitionWidth), 0.0f, 1.0f);
				const auto smoothGate = gatePosition * gatePosition
					* (3.0f - 2.0f * gatePosition);
				const auto gated = driven * smoothGate;
				output = std::clamp((gated + starvation) * 4.0f, -1.0f, 1.0f);
				break;
			}
			case RavMode::wavefold:
			{
				const auto folds = 1.0f + characterValue * 7.0f;
				const auto offset = biasValue * responseValue;
				output = std::sin((driven + offset) * folds) * (0.6f + textureValue * 0.4f);
				break;
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
				output = holdValue;
				break;
			}
		}

		if (mode == RavMode::fuzz || mode == RavMode::wavefold)
		{
			const auto tilt = std::clamp(toneValue / 6.0f, -0.8f, 0.8f);
			fuzzToneState += 0.08f * (output - fuzzToneState);
			output += tilt * (output - fuzzToneState);
		}
		return postStage.process(output, postCutoffHz(mode, textureValue));
	}

	[[nodiscard]] static float postCutoffHz(RavMode currentMode, float textureValue) noexcept
	{
		switch (currentMode)
		{
			case RavMode::saturation: return 10'000.0f + (1.0f - textureValue) * 8'000.0f;
			case RavMode::overdrive: return 11'000.0f + textureValue * 7'000.0f;
			case RavMode::distortion: return 5'000.0f + textureValue * 8'000.0f;
			case RavMode::fuzz: return 4'000.0f + textureValue * 6'000.0f;
			case RavMode::wavefold: return 5'000.0f + textureValue * 8'000.0f;
			case RavMode::bitcrush: return 0.0f;
		}

		return 0.0f;
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
	float fuzzToneState {};
	RavPostStage postStage;
	juce::SmoothedValue<float> drive { 6.0f };
	juce::SmoothedValue<float> bias;
	juce::SmoothedValue<float> character { 0.5f };
	juce::SmoothedValue<float> response { 0.5f };
	juce::SmoothedValue<float> texture { 0.5f };
	juce::SmoothedValue<float> tone;
	double biasRampDuration { 0.02 };
	double textureRampDuration { 0.02 };
};
}
