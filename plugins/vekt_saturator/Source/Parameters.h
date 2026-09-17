#pragma once

#include <vekt/dsp/OversamplingQuality.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

namespace vekt::saturator::parameters
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

inline constexpr auto stateType = "VektSaturatorState";
inline constexpr auto projectStateType = "VektSaturatorProjectState";
inline constexpr auto projectStateVersion = 2;
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
}
