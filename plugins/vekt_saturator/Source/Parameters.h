#pragma once

#include <vekt/dsp/OversamplingQuality.h>

#include <juce_audio_processors/juce_audio_processors.h>

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
inline constexpr auto oversamplingFactor = "oversamplingFactor";
inline constexpr auto oversamplingPhase = "oversamplingPhase";

inline constexpr auto stateType = "VektSaturatorState";
inline constexpr auto projectStateType = "VektSaturatorProjectState";
inline constexpr auto projectStateVersion = 2;

[[nodiscard]] juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
[[nodiscard]] dsp::OversamplingQuality qualityFrom(float factorIndex, float phaseIndex) noexcept;
}
