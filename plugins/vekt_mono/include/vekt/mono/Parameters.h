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
inline constexpr auto heldKeyReturn = "heldKeyReturn";
inline constexpr auto notePriority = "notePriority";
inline constexpr auto quality = "quality";
// Render voices on helper threads too. Saved with the plugin state, not in presets; not automatable.
inline constexpr auto multicore = "multicore";
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
inline constexpr auto osc1Octave = "osc1Octave";
inline constexpr auto osc2Octave = "osc2Octave";
inline constexpr auto osc3Octave = "osc3Octave";
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
inline constexpr auto filterQCompensation = "filterQCompensation";
inline constexpr auto filterMode = "filterMode";
inline constexpr auto filterType = "filterType";
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

// LFO parameters. Each LFO has its source controls, a master Amount and one
// dedicated depth per destination; there is no modulation matrix.
struct LfoParameterIds
{
	const char* rate; const char* sync; const char* division; const char* shape; const char* polarity;
	const char* mode; const char* phase; const char* delay; const char* fade; const char* amount;
	std::array<const char*, 3> pitch, morph, width, level;
	const char* filter; const char* amp; const char* drive; const char* noise; const char* detune; const char* spread;
	const char* filterMode; // sound schema 8

	// The schema-7 parameters, in their saved order; filterMode is listed with the schema-8 parameters.
	[[nodiscard]] constexpr std::array<const char*, 28> all() const noexcept
	{
		return { rate, sync, division, shape, polarity, mode, phase, delay, fade, amount,
			pitch[0], pitch[1], pitch[2], morph[0], morph[1], morph[2], width[0], width[1], width[2], level[0], level[1], level[2],
			filter, amp, drive, noise, detune, spread };
	}

	// Every destination depth: the eighteen of all() in order, then filterMode.
	[[nodiscard]] constexpr std::array<const char*, 19> depths() const noexcept
	{
		return { pitch[0], pitch[1], pitch[2], morph[0], morph[1], morph[2], width[0], width[1], width[2], level[0], level[1], level[2],
			filter, amp, drive, noise, detune, spread, filterMode };
	}
};

inline constexpr std::array lfos {
	LfoParameterIds { "lfo1Rate", "lfo1Sync", "lfo1Division", "lfo1Shape", "lfo1Polarity", "lfo1Mode", "lfo1Phase", "lfo1Delay", "lfo1Fade", "lfo1Amount",
		{ "lfo1Osc1Pitch", "lfo1Osc2Pitch", "lfo1Osc3Pitch" }, { "lfo1Osc1Morph", "lfo1Osc2Morph", "lfo1Osc3Morph" }, { "lfo1Osc1Width", "lfo1Osc2Width", "lfo1Osc3Width" }, { "lfo1Osc1Level", "lfo1Osc2Level", "lfo1Osc3Level" },
		"lfo1Filter", "lfo1Amp", "lfo1Drive", "lfo1Noise", "lfo1Detune", "lfo1Spread", "lfo1FilterMode" },
	LfoParameterIds { "lfo2Rate", "lfo2Sync", "lfo2Division", "lfo2Shape", "lfo2Polarity", "lfo2Mode", "lfo2Phase", "lfo2Delay", "lfo2Fade", "lfo2Amount",
		{ "lfo2Osc1Pitch", "lfo2Osc2Pitch", "lfo2Osc3Pitch" }, { "lfo2Osc1Morph", "lfo2Osc2Morph", "lfo2Osc3Morph" }, { "lfo2Osc1Width", "lfo2Osc2Width", "lfo2Osc3Width" }, { "lfo2Osc1Level", "lfo2Osc2Level", "lfo2Osc3Level" },
		"lfo2Filter", "lfo2Amp", "lfo2Drive", "lfo2Noise", "lfo2Detune", "lfo2Spread", "lfo2FilterMode" }
};

// Performance vibrato: a shared LFO on oscillator pitch, played with the mod wheel and aftertouch.
inline constexpr auto vibratoRate = "vibratoRate";
inline constexpr auto vibratoShape = "vibratoShape";
inline constexpr auto vibratoDepth = "vibratoDepth";
inline constexpr std::array vibratoParameterIds { vibratoRate, vibratoShape, vibratoDepth };

inline constexpr std::array legacySoundParameterIds {
	performanceMode, heldKeyReturn, unison, unisonDetune, unisonSpread, voiceWidth, glideMode, glideTime,
	pitchBendRange, calibration, drift, masterOutput,
	osc1Range, osc2Range, osc3Range, osc1Semitone, osc2Semitone, osc3Semitone,
	osc1Fine, osc2Fine, osc3Fine, osc1Octave, osc2Octave, osc3Octave,
	osc1Level, osc2Level, osc3Level,
	osc1Morph, osc2Morph, osc3Morph, osc1PulseWidth, osc2PulseWidth, osc3PulseWidth,
	noiseType, noiseLevel, filterCutoff, filterResonance, filterKeyTracking,
	filterEnvelopeAmount, filterDrive, ampAttack, ampDecay, ampSustain, ampRelease,
	filterAttack, filterDecay, filterSustain, filterRelease, ampVelocity, filterVelocity, filterQCompensation,
	notePriority };

// Parameters added in sound schema 7: both LFOs and the performance vibrato.
inline constexpr auto schema7ParameterIds = []
{
	std::array<const char*, 2 * 28 + vibratoParameterIds.size()> ids {};
	std::size_t next {};
	for (const auto& lfo : lfos)
		for (const auto* identifier : lfo.all()) ids[next++] = identifier;
	for (const auto* identifier : vibratoParameterIds) ids[next++] = identifier;
	return ids;
}();

// Parameters added in sound schema 8: the ladder's LP-Notch-HP Mode and its LFO depths.
inline constexpr std::array schema8ParameterIds { filterMode, lfos[0].filterMode, lfos[1].filterMode };
// Sound schema 9 added a saturated-taps A/B (filterSaturatedTaps); schema 11 retired it, the saturated taps being
// the only Notch/HP mix since. Parameters added in sound schema 10: the Ladder/SVF filter type (ADR 0006).
inline constexpr std::array schema10ParameterIds { filterType };

inline constexpr auto soundParameterIds = []
{
	std::array<const char*, legacySoundParameterIds.size() + schema7ParameterIds.size() + schema8ParameterIds.size()
		+ schema10ParameterIds.size()> ids {};
	std::size_t next {};
	for (const auto* identifier : legacySoundParameterIds) ids[next++] = identifier;
	for (const auto* identifier : schema7ParameterIds) ids[next++] = identifier;
	for (const auto* identifier : schema8ParameterIds) ids[next++] = identifier;
	for (const auto* identifier : schema10ParameterIds) ids[next++] = identifier;
	return ids;
}();

[[nodiscard]] juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
}
