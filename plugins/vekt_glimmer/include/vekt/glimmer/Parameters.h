#pragma once

#include <vekt/dsp/OversamplingQuality.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

namespace vekt::glimmer::parameters
{
inline constexpr auto inputGain = "inputGain";
inline constexpr auto preampDrive = "preampDrive";
inline constexpr auto hornDrumBalance = "hornDrumBalance";
inline constexpr auto micAngle = "micAngle";
inline constexpr auto micDistance = "micDistance";
inline constexpr auto slowSpeed = "slowSpeed";
inline constexpr auto fastSpeed = "fastSpeed";
inline constexpr auto accelerationTime = "accelerationTime";
inline constexpr auto decelerationTime = "decelerationTime";
inline constexpr auto hornTone = "hornTone";
inline constexpr auto drumTone = "drumTone";
inline constexpr auto speedMode = "speedMode";
inline constexpr auto sensitivity = "sensitivity";
inline constexpr auto autoGain = "autoGain";
inline constexpr auto bypass = "bypass";
inline constexpr auto mix = "mix";
inline constexpr auto outputGain = "outputGain";
inline constexpr auto trackingOversampling = "trackingOversampling";
inline constexpr auto offlineOversampling = "offlineOversampling";

inline constexpr auto stateType = "VektGlimmerState";
inline constexpr auto projectStateType = "VektGlimmerProjectState";
inline constexpr auto presetProductIdentifier = "com.vekt.glimmer";
inline constexpr auto currentFactoryPreset = "currentFactoryPreset";

inline constexpr std::array soundParameterIds {
	inputGain,
	preampDrive,
	hornDrumBalance,
	micAngle,
	micDistance,
	slowSpeed,
	fastSpeed,
	accelerationTime,
	decelerationTime,
	hornTone,
	drumTone,
	speedMode,
	sensitivity,
	autoGain,
	mix,
	outputGain
};

[[nodiscard]] juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
[[nodiscard]] dsp::OversamplingQuality trackingQualityFrom(float index) noexcept;
[[nodiscard]] dsp::OversamplingQuality offlineQualityFrom(float index) noexcept;
}
