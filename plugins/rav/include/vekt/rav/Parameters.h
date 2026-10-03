#pragma once

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
inline constexpr auto shape = "shape";
inline constexpr auto dynamics = "dynamics";
inline constexpr auto texture = "texture";
inline constexpr auto trackingOversampling = "trackingOversampling";
inline constexpr auto offlineOversampling = "offlineOversampling";
inline constexpr auto stageEnabledSaturation = "stageEnabledSaturation";
inline constexpr auto stageEnabledOverdrive = "stageEnabledOverdrive";
inline constexpr auto stageEnabledDistortion = "stageEnabledDistortion";
inline constexpr auto stageEnabledCircuitFuzz = "stageEnabledCircuitFuzz";
inline constexpr auto stageEnabledGatedFuzz = "stageEnabledGatedFuzz";

inline constexpr std::array stageEnabledIds {
	stageEnabledSaturation, stageEnabledOverdrive, stageEnabledDistortion,
	stageEnabledCircuitFuzz, stageEnabledGatedFuzz
};

inline constexpr auto stateType = "VektRavState";
inline constexpr auto presetProductIdentifier = "com.vekt.rav";
// The product name hosts, preset banks and the user preset folder show.
inline constexpr auto productName = "Rav";
// The preset sound schema this build writes and loads.
inline constexpr int presetSoundSchemaVersion = 4;
inline constexpr std::array soundParameterIds {
	inputGain,
	drive,
	tone,
	bias,
	autoGain,
	mix,
	outputGain
	, lowBandMix, midBandMix, highBandMix, lowMidCutoffHz, midHighCutoffHz,
	mode, shape, dynamics, texture
	, stageEnabledSaturation, stageEnabledOverdrive, stageEnabledDistortion,
	stageEnabledCircuitFuzz, stageEnabledGatedFuzz
};

[[nodiscard]] juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

[[nodiscard]] inline float toneSlopeFromUserValue(float value) noexcept
{
	const auto normalized = std::clamp(value / 6.0f, -1.0f, 1.0f);
	const auto shaped = std::tanh(1.5f * normalized) / std::tanh(1.5f);
	return shaped * 6.0f;
}
}
