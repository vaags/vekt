#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

namespace vekt::mono::parameters
{
inline constexpr auto stateType = "VektMonoParameters";
inline constexpr auto projectStateType = "VektMonoProject";
inline constexpr auto presetProductIdentifier = "com.vekt.mono";

inline constexpr auto voiceCount = "voiceCount";
inline constexpr auto performanceMode = "performanceMode";
inline constexpr auto quality = "quality";
inline constexpr auto unison = "unison";
inline constexpr auto unisonDetune = "unisonDetune";
inline constexpr auto unisonSpread = "unisonSpread";
inline constexpr auto voiceWidth = "voiceWidth";
inline constexpr auto glideMode = "glideMode";
inline constexpr auto glideTime = "glideTime";
inline constexpr auto pitchBendRange = "pitchBendRange";
inline constexpr auto calibration = "calibration";
inline constexpr auto drift = "drift";
inline constexpr auto masterOutput = "masterOutput";

inline constexpr auto osc1Range = "osc1Range";
inline constexpr auto osc2Range = "osc2Range";
inline constexpr auto osc3Range = "osc3Range";
inline constexpr auto osc1Semitone = "osc1Semitone";
inline constexpr auto osc2Semitone = "osc2Semitone";
inline constexpr auto osc3Semitone = "osc3Semitone";
inline constexpr auto osc1Fine = "osc1Fine";
inline constexpr auto osc2Fine = "osc2Fine";
inline constexpr auto osc3Fine = "osc3Fine";
inline constexpr auto osc1Level = "osc1Level";
inline constexpr auto osc2Level = "osc2Level";
inline constexpr auto osc3Level = "osc3Level";
inline constexpr auto osc1Morph = "osc1Morph";
inline constexpr auto osc2Morph = "osc2Morph";
inline constexpr auto osc3Morph = "osc3Morph";
inline constexpr auto osc1PulseWidth = "osc1PulseWidth";
inline constexpr auto osc2PulseWidth = "osc2PulseWidth";
inline constexpr auto osc3PulseWidth = "osc3PulseWidth";
inline constexpr auto noiseType = "noiseType";
inline constexpr auto noiseLevel = "noiseLevel";

inline constexpr auto filterCutoff = "filterCutoff";
inline constexpr auto filterResonance = "filterResonance";
inline constexpr auto filterKeyTracking = "filterKeyTracking";
inline constexpr auto filterEnvelopeAmount = "filterEnvelopeAmount";
inline constexpr auto filterDrive = "filterDrive";
inline constexpr auto ampAttack = "ampAttack";
inline constexpr auto ampDecay = "ampDecay";
inline constexpr auto ampSustain = "ampSustain";
inline constexpr auto ampRelease = "ampRelease";
inline constexpr auto filterAttack = "filterAttack";
inline constexpr auto filterDecay = "filterDecay";
inline constexpr auto filterSustain = "filterSustain";
inline constexpr auto filterRelease = "filterRelease";
inline constexpr auto ampVelocity = "ampVelocity";
inline constexpr auto filterVelocity = "filterVelocity";

inline constexpr std::array soundParameterIds {
	performanceMode, unison, unisonDetune, unisonSpread, voiceWidth, glideMode, glideTime,
	pitchBendRange, calibration, drift, masterOutput,
	osc1Range, osc2Range, osc3Range, osc1Semitone, osc2Semitone, osc3Semitone,
	osc1Fine, osc2Fine, osc3Fine, osc1Level, osc2Level, osc3Level,
	osc1Morph, osc2Morph, osc3Morph, osc1PulseWidth, osc2PulseWidth, osc3PulseWidth,
	noiseType, noiseLevel, filterCutoff, filterResonance, filterKeyTracking,
	filterEnvelopeAmount, filterDrive, ampAttack, ampDecay, ampSustain, ampRelease,
	filterAttack, filterDecay, filterSustain, filterRelease, ampVelocity, filterVelocity };

[[nodiscard]] juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
}