#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <cmath>
#include <numbers>

namespace vekt::mono
{
constexpr float referenceOscillationFeedback = 4.58f;

struct LadderCoefficients
{
	float stageCoefficient {};
	float oscillationFeedback {};
};

inline LadderCoefficients ladderCoefficients(float cutoff, float sampleRate) noexcept
{
	// The feedback sample is one sample old, in addition to the phase shift of
	// the four one-pole stages. Solve that complete loop at the requested cutoff
	// so Resonance peaks and self-oscillates at the frequency shown by Cutoff.
	const auto omega = juce::jlimit(1.0e-6f, std::numbers::pi_v<float> * 0.9f,
		2.0f * std::numbers::pi_v<float> * cutoff / sampleRate);
	const auto stagePhase = (std::numbers::pi_v<float> - omega) * 0.25f;
	const auto tangent = std::tan(stagePhase);
	const auto pole = tangent / (std::sin(omega) + tangent * std::cos(omega));
	const auto coefficient = 1.0f - pole;
	const auto stageMagnitude = coefficient
		/ std::sqrt(1.0f + pole * pole - 2.0f * pole * std::cos(omega));
	return { coefficient, 1.0f / std::pow(stageMagnitude, 4.0f) };
}

// Historical offline comparator only; not linked into the Mono render path.
class DelayedFeedbackLadder
{
public:
	void reset() noexcept { state = {}; }

	[[nodiscard]] float process(float input, float cutoff, float sampleRate,
		float resonanceAmount, float driveGain) noexcept
	{
		const auto coefficients = ladderCoefficients(cutoff, sampleRate);
		// Preserve the established control taper at the 1 kHz / 48 kHz reference,
		// but scale it by this cutoff's actual oscillation threshold. Resonance then
		// has the same meaning as Cutoff or sample rate changes.
		const auto normalizedResonance = juce::jlimit(0.0f, 1.0f, resonanceAmount);
		const auto regenerationPosition = juce::jlimit(0.0f, 1.0f,
			(normalizedResonance - 0.65f) / 0.2f);
		const auto regeneration = regenerationPosition * regenerationPosition
			* (3.0f - 2.0f * regenerationPosition);
		const auto referenceFeedback = 4.05f * std::pow(normalizedResonance, 0.72f)
			+ 3.0f * regeneration;
		const auto feedbackAmount = coefficients.oscillationFeedback * referenceFeedback
			/ referenceOscillationFeedback;
		const auto feedback = state[3] * feedbackAmount;
		const auto stateEnergy = std::abs(state[0]) + std::abs(state[1])
			+ std::abs(state[2]) + std::abs(state[3]);
		const auto startupExcitation = regeneration > 0.0f && stateEnergy < 1.0e-12f
			? 1.0e-4f * regeneration : 0.0f;
		constexpr auto ladderHeadroom = 4.0f;
		const auto summingNode = (input + startupExcitation) * driveGain - feedback;
		auto signal = ladderHeadroom * std::tanh(summingNode / ladderHeadroom);
		for (auto& stage : state)
		{
			const auto saturated = ladderHeadroom * std::tanh(signal / ladderHeadroom);
			stage += coefficients.stageCoefficient * (saturated - stage);
			signal = stage;
		}
		return signal / std::sqrt(driveGain);
	}

private:
	std::array<float, 4> state {};
};
}