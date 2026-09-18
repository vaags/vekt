#pragma once

#include <vekt/dsp/OversamplingQuality.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <cmath>

namespace vekt::rav::parameters
{
inline constexpr auto inputGain = "inputGain";
inline constexpr auto drive = "drive";
inline constexpr auto tone = "tone";
inline constexpr auto bias = "bias";
inline constexpr auto autoGain = "autoGain";
inline constexpr auto bypass = "bypass";
inline constexpr auto mix = "mix";
inline constexpr auto outputGain = "outputGain";
inline constexpr auto lowBandMix = "lowBandMix";
inline constexpr auto midBandMix = "midBandMix";
inline constexpr auto highBandMix = "highBandMix";
inline constexpr auto lowMidCutoffHz = "lowMidCutoffHz";
inline constexpr auto midHighCutoffHz = "midHighCutoffHz";
inline constexpr auto mode = "mode";
inline constexpr auto character = "character";
inline constexpr auto response = "response";
inline constexpr auto texture = "texture";
inline constexpr auto trackingOversampling = "trackingOversampling";
inline constexpr auto offlineOversampling = "offlineOversampling";
inline constexpr auto stageEnabledSaturation = "stageEnabledSaturation";
inline constexpr auto stageEnabledOverdrive = "stageEnabledOverdrive";
inline constexpr auto stageEnabledDistortion = "stageEnabledDistortion";
inline constexpr auto stageEnabledFuzz = "stageEnabledFuzz";

inline constexpr std::array stageEnabledIds {
	stageEnabledSaturation, stageEnabledOverdrive, stageEnabledDistortion,
	stageEnabledFuzz
};

inline constexpr auto stateType = "VektRavState";
inline constexpr auto projectStateType = "VektRavProjectState";
inline constexpr auto presetProductIdentifier = "com.vekt.rav";
inline constexpr auto currentFactoryPreset = "currentFactoryPreset";
inline constexpr std::array soundParameterIds {
	inputGain,
	drive,
	tone,
	bias,
	autoGain,
	mix,
	outputGain
	, lowBandMix, midBandMix, highBandMix, lowMidCutoffHz, midHighCutoffHz,
	mode, character, response, texture
};

[[nodiscard]] juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
[[nodiscard]] dsp::OversamplingQuality trackingQualityFrom(float index) noexcept;
[[nodiscard]] dsp::OversamplingQuality offlineQualityFrom(float index) noexcept;

[[nodiscard]] inline float toneSlopeFromUserValue(float value) noexcept
{
	const auto normalized = std::clamp(value / 6.0f, -1.0f, 1.0f);
	const auto shaped = std::tanh(1.5f * normalized) / std::tanh(1.5f);
	return shaped * 6.0f;
}
}
