#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>

namespace vekt::kobber
{
struct KobberVoiceSettings;

// One LFO's parameters (parameters::LfoParameterIds), resolved by ID.
struct KobberLfoParameterValues
{
	std::atomic<float>*rate {}, *sync {}, *division {}, *shape {}, *polarity {}, *mode {}, *phase {}, *delay {},
	    *fade {}, *amount {};
	std::array<std::atomic<float>*, 19> depths {}; // in parameters::LfoParameterIds::depths() order
};

// Every parameter Mono's processor reads, resolved by ID once (at construction) so no block looks one up by name.
struct KobberParameterValues
{
	std::array<std::atomic<float>*, 3> range {}, semitone {}, fine {}, octave {}, level {}, morph {}, pulseWidth {};
	std::atomic<float>*noiseType {}, *noiseLevel {}, *filterCutoff {}, *filterResonance {}, *filterKeyTracking {},
	    *filterEnvelopeAmount {};
	std::atomic<float>*filterDrive {}, *filterQCompensation {}, *filterMode {}, *filterType {}, *ampAttack {},
	    *ampDecay {};
	std::atomic<float>*ampSustain {}, *ampRelease {}, *filterAttack {}, *filterDecay {}, *filterSustain {},
	    *filterRelease {};
	std::atomic<float>*ampVelocity {}, *filterVelocity {}, *calibration {}, *unison {}, *unisonDetune {},
	    *unisonSpread {};
	std::atomic<float>*voiceWidth {}, *drift {}, *glideMode {}, *glideTime {}, *multicore {}, *voiceCount {};
	std::atomic<float>*pitchBendRange {}, *performanceMode {}, *notePriority {}, *heldKeyReturn {}, *vibratoRate {};
	std::atomic<float>*vibratoShape {}, *vibratoDepth {}, *vibratoAmount {}, *masterOutput {};
	std::array<KobberLfoParameterValues, 2> lfos {};

	// Stops if the state lacks one (plugin_support::requireParameter).
	[[nodiscard]] static KobberParameterValues resolve(juce::AudioProcessorValueTreeState& state);
};

// The voice settings the parameters describe now, in the voice's units: the one mapping from Mono's parameters to its
// voices, used by the processor for every block and note and by tests that render a voice directly. Tempo-synced LFO
// rates follow transportBpm. Audio-thread safe.
[[nodiscard]] KobberVoiceSettings voiceSettingsFrom(const KobberParameterValues& cached, double transportBpm) noexcept;
}
