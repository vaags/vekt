#pragma once

#include "Korg35Response.h"
#include "LadderPoleMix.h"
#include "LadderResonance.h"
#include "NonlinearTptKorg35.h"
#include "NonlinearTptSvf.h"
#include "SvfResponse.h"
#include "NonlinearTptLadder.h"
#include "NonlinearTptLadderHighPass.h"
#include "WidthOscillator.h"
#include "ContourEnvelope.h"
#include <vekt/dsp/LinearRamp.h>
#include "Glide.h"
#include "FilterLimits.h"
#include "Lfo.h"
#include "KobberChoiceTypes.h"

#include <vekt/dsp/DcBlocker.h>

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace vekt::kobber
{
constexpr float twoPi = 2.0f * std::numbers::pi_v<float>;
constexpr float maximumContourOctaves = 8.0f;
constexpr float maximumVelocityOctaves = 4.0f;
constexpr float voiceTransitionSeconds = 0.003f;
// The Ladder's high-pass ladder rests only when no LFO reaches Mode and Mode has stayed at LP this long, so a knob or
// automation move that passes through LP does not restart it (ADR 0009).
constexpr float ladderHighPassRestSeconds = 1.0f;

// The filter-output DC blocker (ADR 0008): oscillators and filters may carry or generate DC (the raw Width policy, and
// every filter's saturation on waveforms without half-wave symmetry), and the voice removes it once, per unison layer,
// where the filter output meets the amp envelope. First order: -3 dB at 5 Hz, about -0.26 dB at 20 Hz.
inline constexpr double filterOutputDcBlockerHz = 5.0;

// Width of the random phase offsets a new note gives its unison layers, in cycles: none at 0 cents (the layers
// start as identical copies), fully random from this detune up.
inline constexpr float unisonDecorrelatedDetuneCents = 5.0f;
[[nodiscard]] inline float unisonSpreadFactor(float detuneCents) noexcept
{
	return std::clamp(detuneCents / unisonDecorrelatedDetuneCents, 0.0f, 1.0f);
}

// Gain for a sum of `layers` unison layers whose phases are spread by s cycles (0..1): 1/N for identical copies
// and 1/sqrt(N) for decorrelated layers, so unison keeps the 1x level on average.
[[nodiscard]] inline float unisonGain(int layers, float spread) noexcept
{
	return layers <= 1 ? 1.0f : std::pow(static_cast<float>(layers), 0.5f * spread - 1.0f);
}

// Correlation between the layers' noise that makes the summed noise keep the 1x level under unisonGain:
// N + N(N - 1) rho = N^(2 - s), so identical noise at s = 0 and independent noise at s = 1.
[[nodiscard]] inline float unisonNoiseCorrelation(int layers, float spread) noexcept
{
	if (layers <= 1) return 1.0f;
	const auto count = static_cast<float>(layers);
	return (std::pow(count, 1.0f - spread) - 1.0f) / (count - 1.0f);
}

inline float dbToGain(float decibels) noexcept { return std::pow(10.0f, decibels / 20.0f); }
inline float midiToHz(float note) noexcept { return 440.0f * std::exp2((note - 69.0f) / 12.0f); }
// One LFO's source settings plus its per-destination depths, already scaled by Amount.
struct KobberLfoSettings
{
	Lfo::Parameters source;
	std::array<float, 3> pitch {}, morph {}, width {}, level {}; // semitones, morph units, pulse-width %, level 0..1
	float filter {}, amp {}, drive {}, noise {}, detune {}, spread {}; // octaves, gain, dB, level, cents, spread
	float filterMode {}; // Mode units (-1 LP .. +1 HP)
};

inline constexpr std::size_t lfoCount = 2;

// Width DC policy (not a saved/automatable parameter until the sound policy is decided): raw keeps each
// anchor's frozen-Width mean, zeroCentered removes it.
enum class WidthDcPolicy { raw, zeroCentered };


struct KobberVoiceSettings
{
	std::array<int, 3> rangeOctaves {}; // Range (footage) in octaves from 8': 16' is -1, 1' is 3
	std::array<float, 3> semitone {}, fine {}, octave {}, level {}, morph {}, pulseWidth {};
	float noiseLevel {}, cutoff {}, resonance {}, tracking {}, envelopeAmount {}, drive {};
	float ampAttack {}, ampDecay {}, ampSustain {}, ampRelease {};
	float filterAttack {}, filterDecay {}, filterSustain {}, filterRelease {};
	float ampVelocity {}, filterVelocity {}, calibration {};
	float detune {}, unisonSpread {}, voiceWidth {}, glideTime {}, drift {};
	int unison {};
	GlideMode glideMode { GlideMode::off };
	NoiseType noiseType { NoiseType::off };
	bool qCompensation {};
	float filterMode { -1.0f };
	FilterType filterType { FilterType::ladder };
	std::array<KobberLfoSettings, lfoCount> lfo {};
	WidthDcPolicy widthDcPolicy { WidthDcPolicy::raw };
};

// Where the LFO sum lands for one sample. Every field is an offset that is exactly zero at zero depth.
struct KobberModulation
{
	std::array<float, 3> pitch {}, morph {}, width {}, level {};
	float filter {}, amp {}, drive {}, noise {}, detune {}, spread {}, filterMode {};
};

// Drift (0..100 %) models analog instability: each oscillator of each voice and unison layer wanders slowly
// and independently (Sequential-style oscillator "slop"), each voice has fixed per-oscillator tuning offsets,
// and each voice slot has fixed "voice card" tolerances (Prophet-5 rev4-style Vintage). The knob is
// progressive: driftAmount scales the depths below (1 at about 52 %, 4 at 100 %), and driftSpeed makes the
// wandering up to 3x faster at the top, so low settings stay subtle and 100 % is plainly unstable.
inline constexpr float driftWanderCents = 7.0f;         // wandering pitch, per oscillator and layer
inline constexpr float driftStaticCents = 3.0f;         // fixed per-voice, per-oscillator tuning offset
inline constexpr float driftCutoffOctaves = 0.057f;     // ~ +/-4 % cutoff per voice
inline constexpr float driftEnvelopeTimeScale = 0.06f;  // +/-6 % attack, decay and release per voice
inline constexpr float driftLevelScale = 0.035f;        // ~ +/-0.3 dB per voice

// Depth multiplier for Drift 0..100 %: d + 3 d^3 (0.10 at 10 %, 0.38 at 30 %, 0.88 at 50 %, 4 at 100 %).
[[nodiscard]] inline float driftAmount(float percent) noexcept
{
	const auto d = std::clamp(percent * 0.01f, 0.0f, 1.0f);
	return d + 3.0f * d * d * d;
}

// Wander speed multiplier for Drift 0..100 %: 1 + 2 d^2 (3x at 100 %).
[[nodiscard]] inline float driftSpeed(float percent) noexcept
{
	const auto d = std::clamp(percent * 0.01f, 0.0f, 1.0f);
	return 1.0f + 2.0f * d * d;
}

// A smooth random walk in -1..1: a new random target every 1.5-4 s (divided by speed), followed through two
// one-pole stages (0.6 s / speed each), so the motion stays below about 0.5 Hz x speed without steps.
// Two one-poles toward a random target that changes every few seconds. Double, so the walk reaches its targets at
// every internal rate (ARCHITECTURE.md, DSP Contracts).
struct DriftWalk
{
	double value {}, stage {}, target {};
	int samplesToNextTarget {};

	void reset(juce::Random& random, double sampleRate) noexcept
	{
		target = random.nextFloat() * 2.0f - 1.0f;
		stage = value = target;
		samplesToNextTarget = static_cast<int>(random.nextFloat() * 4.0 * sampleRate);
	}

	float next(juce::Random& random, double sampleRate, double coefficient, float speed = 1.0f) noexcept
	{
		if (--samplesToNextTarget <= 0)
		{
			target = random.nextFloat() * 2.0f - 1.0f;
			samplesToNextTarget = static_cast<int>((1.5 + 2.5 * random.nextFloat()) * sampleRate / speed);
		}
		stage += (target - stage) * coefficient;
		value += (stage - value) * coefficient;
		return static_cast<float>(value);
	}
};

class KobberVoice
{
public:
	// newSampleRate is the rate render() runs at (oversampled); hostSampleRate (default: the same) sets
	// the oscillator's spectral guard, which is defined at the host Nyquist.
	void prepare(double newSampleRate, std::uint32_t seed, double hostSampleRate = 0.0);

	void reset();

	void start(int newChannel, int newNote, float newVelocity, const KobberVoiceSettings& settings, bool retrigger, bool legato, std::uint64_t newAge);

	void release(bool keepSustained);

	void releaseSustain();

	void stop();

	// vibratoSemitones is the shared vibrato LFO at full depth; channelControl is the channel's mod wheel or pressure.
	// One sample of the voice. The processor splits this into beginSample, one batched ladder solve over all
	// voices' layers, and finishSample (or finishSvfSample); render does the same for a single voice.
	void render(float& left, float& right, const KobberVoiceSettings& settings, float bend,
		const std::array<double, lfoCount>& lfoClockPositions = {}, float vibratoSemitones = 0.0f, float channelControl = 0.0f)
	{
		if (!beginSample(settings, bend, lfoClockPositions, vibratoSemitones, channelControl)) return;
		if (settings.filterType != FilterType::ladder)
		{
			finishNonLadderSample(left, right);
			return;
		}
		const auto layers = static_cast<std::size_t>(pending.layers);
		std::array<float, 4> ladderOutputs {};
		prepareLadderSolve();
		NonlinearTptLadder::processCoupled(std::span(filterLadders.data(), layers), std::span<const float>(pending.mixers.data(), layers),
			std::span(ladderOutputs.data(), layers), ladderSolveSettings);
		finishLadderSample(ladderOutputs.data(), left, right);
	}

	// The ladder's settings for this sample's solve: Mode capped at Notch, since above Notch the voice crossfades from the
	// ladder's Notch into the high-pass ladder (finishLadderSample; ADR 0009).
	void prepareLadderSolve() noexcept
	{
		ladderSolveSettings = pending.ladderSettings;
		ladderSolveSettings.mode = std::min(ladderSolveSettings.mode, 0.0f);
	}

	// After the ladder solve, given each layer's output. Up to Notch that output is the voice's filter (the tap mix,
	// LP -> Notch). Across Notch -> HP it crossfades by filterModeEase(Mode) from the ladder's Notch into the high-pass
	// ladder (NonlinearTptLadderHighPass: linear stages, saturating feedback, short of self-oscillation), chosen by ear
	// over a low-pass-to-high-pass crossfade and a snap at Notch (ADR 0009). The high-pass ladder runs whenever Mode is
	// above LP, so it is settled before the crossfade reaches it. It rests (reset) only where most patches sit: Mode at LP
	// for ladderHighPassRestSeconds with no LFO on Mode. It then restarts primed; a restart still rings against the warm
	// filter it replaces (about as loudly as the signal on a square Mode LFO), so modulated Mode never rests.
	void finishLadderSample(const float* ladderOutputs, float& left, float& right) noexcept
	{
		const auto& filter = pending.ladderSettings;
		if (filter.mode <= -1.0f)
		{
			if (!highPassResting)
			{
				highPassSamplesAtLowPass = pending.modeModulated ? 0 : highPassSamplesAtLowPass + 1;
				if (highPassSamplesAtLowPass >= static_cast<int>(ladderHighPassRestSeconds * sampleRate))
				{
					for (auto& highPass : filterHighPasses) highPass.reset();
					highPassResting = true;
				}
			}
			if (highPassResting)
			{
				finishSample(ladderOutputs, left, right);
				return;
			}
		}
		else highPassSamplesAtLowPass = 0;
		NonlinearTptLadderHighPassSettings settings;
		settings.cutoffHz = static_cast<double>(filter.cutoffHz);
		settings.feedback = ladderHighPassFeedback(static_cast<double>(filter.resonance));
		settings.driveDecibels = static_cast<double>(filter.driveDecibels);
		const auto coefficients = filterHighPasses[0].coefficients(settings); // the layers share one rate and settings
		if (highPassResting)
		{
			// Restarted on a running signal: from its steady state, so it does not ring the step.
			for (std::size_t stack = 0; stack < static_cast<std::size_t>(pending.layers); ++stack)
				filterHighPasses[stack].prime(static_cast<double>(pending.mixers[stack]), coefficients);
			highPassResting = false;
		}
		const auto blend = filter.mode <= 0.0f ? 0.0 : filterModeEase(std::min(1.0, static_cast<double>(filter.mode)));
		std::array<float, 4> outputs {};
		for (std::size_t stack = 0; stack < static_cast<std::size_t>(pending.layers); ++stack)
		{
			const auto highPass = filterHighPasses[stack].process(static_cast<double>(pending.mixers[stack]), coefficients);
			outputs[stack] = blend > 0.0 ? static_cast<float>((1.0 - blend) * static_cast<double>(ladderOutputs[stack]) + blend * highPass)
				: ladderOutputs[stack];
		}
		finishSample(outputs.data(), left, right);
	}

	// Everything before the ladder: modulation, envelopes, oscillators and mixers. Returns false (and renders
	// nothing) when the voice is silent; otherwise ladderLanes/ladderRequest describe the solve finishSample needs.
	bool beginSample(const KobberVoiceSettings& settings, float bend,
		const std::array<double, lfoCount>& lfoClockPositions = {}, float vibratoSemitones = 0.0f, float channelControl = 0.0f)
	{
		if (!active) return false;
		selectFilterType(settings.filterType);
		filterRunning = true;
		// Refresh the parameters at each rendered segment (the processor snapshots
		// automation at MIDI/block boundaries), including while a key is held.
		amp.setParameters(ampEnvelopeParameters(settings));
		filterEnvelope.setParameters(filterEnvelopeParameters(settings));
		auto modulation = nextModulation(settings, lfoClockPositions);
		vibratoControl += (std::max(channelControl, polyPressure) - vibratoControl) * vibratoSmoothing;
		for (auto& pitch : modulation.pitch) pitch += vibratoSemitones * static_cast<float>(vibratoControl);
		const auto playedNote = glide.next(gliding && settings.glideMode != GlideMode::off, settings.glideTime, sampleRate) + bend;
		const auto baseHz = midiToHz(playedNote);
		const auto unisonCount = settings.unison;
		const auto filterEnvelopeValue = filterEnvelope.getNextSample();
		updateFilterControlTargets(settings);
		const auto filterCutoff = std::exp2(nextOf(cutoffOctaves));
		const auto filterResonance = nextOf(resonance);
		const auto filterDriveDb = juce::jlimit(0.0f, 24.0f, nextOf(driveDecibels) + modulation.drive);
		const auto inputCompensation = nextOf(qInputCompensation);
		const auto mode = juce::jlimit(-1.0f, 1.0f, nextOf(filterMode) + modulation.filterMode);
		// No post-ladder resonance boost: isolate the ladder's own onset.
		// Q compensation still acts only on driven input inside its feedback equation.
		const auto velocityGain = (1.0f - settings.ampVelocity) + settings.ampVelocity * velocityCurve;
		const auto allocationFade = fadeInSamples > 0
			? 1.0f - static_cast<float>(fadeInSamples--) / static_cast<float>(transitionLength())
			: 1.0f;
		const auto drift = driftAmount(settings.drift);
		const auto wanderSpeed = driftSpeed(settings.drift);
		if (std::abs(wanderSpeed - driftCoefficientSpeed) > 0.0f)
		{
			driftCoefficientSpeed = wanderSpeed;
			driftCoefficient = 1.0 - std::exp(-static_cast<double>(wanderSpeed) / (0.6 * sampleRate));
		}
		const auto amplitude = amp.getNextSample() * velocityGain * allocationFade * juce::jlimit(0.0f, 1.0f, 1.0f + modulation.amp)
			* (1.0f + drift * driftLevelScale * driftLevel);
		const auto detune = juce::jlimit(0.0f, 50.0f, settings.detune + modulation.detune);
		// The layers' phase spread only grows during a note: by the drift between neighbouring layers at the
		// current detune. Gain follows this spread, not the detune setting, so automating detune while a note
		// holds changes the level only as fast as the layers actually drift apart.
		if (unisonCount > 1 && unisonPhaseSpread < 1.0f)
		{
			// Drift's per-layer wander separates the layers too, even at zero Detune; its depth is a typical
			// pitch difference between two independent walks.
			const auto neighbourCents = 2.0f * detune / static_cast<float>(unisonCount - 1) + drift * driftWanderCents;
			unisonPhaseSpread = std::min(1.0, unisonPhaseSpread + static_cast<double>(baseHz * (std::exp2(neighbourCents / 1'200.0f) - 1.0f)) / sampleRate);
		}
		const auto noiseCorrelation = unisonNoiseCorrelation(unisonCount, static_cast<float>(unisonPhaseSpread));
		if (settings.noiseType == NoiseType::off) noiseHoldRemaining = 0; // holds restart when noise is switched on
		else if (--noiseHoldRemaining < 0)
		{
			noiseHoldRemaining = noiseHoldSamples - 1;
			heldSharedNoise = random.nextFloat() * 2.0f - 1.0f;
			for (int layer = 0; layer < unisonCount; ++layer)
				heldLayerNoise[static_cast<std::size_t>(layer)] = noiseCorrelation < 1.0f ? random.nextFloat() * 2.0f - 1.0f : 0.0f;
		}
		const auto sharedNoise = settings.noiseType != NoiseType::off ? heldSharedNoise : 0.0f;
		const auto unisonSpread = juce::jlimit(0.0f, 1.0f, settings.unisonSpread + modulation.spread);
		const auto noiseLevel = juce::jlimit(0.0f, 1.0f, settings.noiseLevel + modulation.noise);
		std::array<float, 3> levels {}, widths {};
		auto& morphs = lastMorphs;
		for (std::size_t oscillator = 0; oscillator < 3; ++oscillator)
		{
			levels[oscillator] = juce::jlimit(0.0f, 1.0f, settings.level[oscillator] + modulation.level[oscillator]);
			auto& baseMorph = baseMorphs[oscillator];
			// Morph is cyclic: the ramp runs unwrapped and takes the short way round, so crossing the 4-to-0 wrap on
			// the knob (e.g. 3.9 to 0.1) does not sweep back through saw and triangle.
			const auto step = morphsInitialized ? morphDistance(targetOf(baseMorph), settings.morph[oscillator]) : 0.0f;
			if (morphsInitialized && std::abs(step) > 1.0e-6f)
				baseMorph.setTargetValue(targetOf(baseMorph) + step);
			else if (!morphsInitialized || (!baseMorph.isSmoothing() && !juce::approximatelyEqual(targetOf(baseMorph), settings.morph[oscillator])))
				baseMorph.setCurrentAndTargetValue(settings.morph[oscillator]); // first sample, or re-anchor after turns round the cycle
			morphs[oscillator] = wrapMorph(nextOf(baseMorph) + modulation.morph[oscillator]);
			auto& baseWidth = baseWidths[oscillator];
			if (!morphsInitialized) baseWidth.setCurrentAndTargetValue(settings.pulseWidth[oscillator]);
			else if (!juce::approximatelyEqual(targetOf(baseWidth), settings.pulseWidth[oscillator])) baseWidth.setTargetValue(settings.pulseWidth[oscillator]);
			widths[oscillator] = juce::jlimit(5.0f, 95.0f, nextOf(baseWidth) + modulation.width[oscillator]);
		}
		morphsInitialized = true;
		// All layers share one ladder setting; their ladders are solved together after the mixers are built.
		const auto ladderSettings = filterSettings(settings, filterEnvelopeValue, filterCutoff, filterResonance,
			filterDriveDb, inputCompensation, mode, playedNote, modulation.filter + drift * driftCutoffOctaves * driftCutoff);
		auto& mixers = pending.mixers;
		auto& pans = pending.pans;
		mixers = {};
		pans = {};
		for (int stack = 0; stack < unisonCount; ++stack)
		{
			const auto normalizedStack = unisonCount == 1 ? 0.0f : (2.0f * static_cast<float>(stack) / static_cast<float>(unisonCount - 1) - 1.0f);
			auto& mixer = mixers[static_cast<std::size_t>(stack)];
			for (int oscillator = 0; oscillator < 3; ++oscillator)
			{
				// The walk runs whether or not Drift is up, so turning Drift up mid-note joins its motion smoothly.
				const auto wander = driftWalks[static_cast<std::size_t>(stack)][static_cast<std::size_t>(oscillator)]
					.next(driftRandom, sampleRate, driftCoefficient, wanderSpeed);
				const auto cents = settings.fine[static_cast<std::size_t>(oscillator)] + normalizedStack * detune
					+ drift * (driftWanderCents * wander + driftStaticCents * driftTuning[static_cast<std::size_t>(oscillator)]);
				// Range (footage) and Octave in octaves, semitones, cents and pitch modulation in one exp2.
				const auto frequency = baseHz * std::exp2(static_cast<float>(settings.rangeOctaves[static_cast<std::size_t>(oscillator)])
					+ settings.octave[static_cast<std::size_t>(oscillator)]
					+ (settings.semitone[static_cast<std::size_t>(oscillator)] + cents * 0.01f + modulation.pitch[static_cast<std::size_t>(oscillator)]) / 12.0f);
				const auto phaseIncrement = static_cast<double>(frequency) / sampleRate;
				auto& oscillatorPhase = phase[static_cast<std::size_t>(stack)][static_cast<std::size_t>(oscillator)];
				oscillatorPhase += phaseIncrement;
				oscillatorPhase -= std::floor(oscillatorPhase);
				const auto morph = morphs[static_cast<std::size_t>(oscillator)];
				const auto width = widths[static_cast<std::size_t>(oscillator)];
				const auto level = levels[static_cast<std::size_t>(oscillator)];
				// Silent oscillators are skipped; their phase still runs.
				if (level > 0.0f)
					mixer += renderWidthOscillator(widthStates[static_cast<std::size_t>(stack)][static_cast<std::size_t>(oscillator)],
						oscillatorPhase, frequency, hostRate, morph, width, settings.widthDcPolicy == WidthDcPolicy::zeroCentered) * level;
			}
			if (settings.noiseType != NoiseType::off)
			{
				// Layer noise has the same correlation the gain assumes; identical layers share one noise source.
				auto noise = sharedNoise;
				if (noiseCorrelation < 1.0f)
					noise = std::sqrt(noiseCorrelation) * sharedNoise + std::sqrt(1.0f - noiseCorrelation) * heldLayerNoise[static_cast<std::size_t>(stack)];
				if (settings.noiseType == NoiseType::pink)
				{
					auto& pink = pinkStates[static_cast<std::size_t>(stack)];
					pink = pinkCoefficient * pink + (1.0 - pinkCoefficient) * noise;
					noise = static_cast<float>(pink);
				}
				mixer += noise * noiseLevel;
			}
			pans[static_cast<std::size_t>(stack)] = juce::jlimit(-1.0f, 1.0f, settings.voiceWidth * panPosition
				+ normalizedStack * unisonSpread);
		}
		pending.layers = unisonCount;
		pending.modeModulated = std::any_of(settings.lfo.begin(), settings.lfo.end(),
			[](const KobberLfoSettings& lfo) { return std::abs(lfo.filterMode) > 0.0f; });
		pending.amplitude = amplitude;
		pending.ladderSettings = ladderSettings;
		return true;
	}

	// The ladder solve beginSample asks for: one lane per unison layer, all with this voice's settings.
	[[nodiscard]] int ladderLanes() const noexcept { return pending.layers; }
	void ladderRequest(NonlinearTptLadder** ladders, float* inputs, const NonlinearTptLadderSettings** settings) noexcept
	{
		prepareLadderSolve();
		for (std::size_t stack = 0; stack < static_cast<std::size_t>(pending.layers); ++stack)
		{
			ladders[stack] = &filterLadders[stack];
			inputs[stack] = pending.mixers[stack];
			settings[stack] = &ladderSolveSettings;
		}
	}

	// finishSample for the SVF: filters each unison layer through its own nonlinear SVF with the cutoff, resonance,
	// Drive and smoothed Mode the ladder would get, mixed LP -> Notch -> HP from the SVF's native outputs, then trimmed
	// for Resonance (svfOutputTrim). The voicing constants in SvfResponse.h are locked (ADR 0006).
	void finishSvfSample(float& left, float& right) noexcept
	{
		const auto& filter = pending.ladderSettings;
		const NonlinearTptSvfSettings svfSettings { static_cast<double>(filter.cutoffHz),
			svfDamping(static_cast<double>(filter.resonance)), static_cast<double>(filter.driveDecibels), svfKnee,
			svfDampingCurve };
		const auto trim = svfOutputTrim(static_cast<double>(filter.resonance));
		std::array<float, 4> outputs {};
		for (std::size_t stack = 0; stack < static_cast<std::size_t>(pending.layers); ++stack)
		{
			const auto out = filterSvfs[stack].process(static_cast<double>(pending.mixers[stack]), svfSettings);
			outputs[stack] = static_cast<float>(trim * svfModeMix(static_cast<double>(filter.mode), out.lowPass, out.highPass));
		}
		finishSample(outputs.data(), left, right);
	}

	// finishSample for whichever non-ladder type beginSample selected.
	void finishNonLadderSample(float& left, float& right) noexcept
	{
		if (filterType == FilterType::svf) finishSvfSample(left, right);
		else finishKorg35Sample(left, right);
	}

	// finishSample for K35 (ADR 0007): each unison layer through its own reduced early-Korg35 filter with the cutoff and
	// Drive the ladder would get and Resonance through korg35Feedback (Korg35Response.h), then K35's own Resonance trim
	// (korg35OutputTrim). Mode moves its input from the low-pass input to the MS-20's high-pass input (C2): b = (Mode + 1) / 2,
	// so -1 is the low-pass, +1 the 6 dB/oct high-pass and 0 the full signal with a resonant bell. Q Comp does not apply. Its DC (the limiter partly rectifies inputs
	// without half-wave symmetry) is removed by the voice's filter-output blocker in finishSample, as for every filter;
	// the filter's own feedback keeps the unblocked limiter output.
	// A single voice (render): its own layers as one batched solve. The processor instead batches every voice of a
	// render unit through korg35Lanes / korg35Request / finishKorg35Sample(outputs).
	void finishKorg35Sample(float& left, float& right) noexcept
	{
		std::array<NonlinearTptKorg35*, 4> filters {};
		std::array<const NonlinearTptKorg35Settings*, 4> settings {};
		std::array<double, 4> inputs {}, outputs {};
		korg35Request(filters.data(), inputs.data(), settings.data());
		const auto lanes = static_cast<std::size_t>(korg35Lanes());
		NonlinearTptKorg35::processLanes(std::span<NonlinearTptKorg35* const>(filters.data(), lanes), std::span<const double>(inputs.data(), lanes),
			std::span(outputs.data(), lanes), std::span<const NonlinearTptKorg35Settings* const>(settings.data(), lanes));
		finishKorg35Sample(outputs.data(), left, right);
	}

	// The K35 solve this sample needs, one lane per unison layer, all with this voice's settings.
	[[nodiscard]] int korg35Lanes() const noexcept { return pending.layers; }
	void korg35Request(NonlinearTptKorg35** filters, double* inputs, const NonlinearTptKorg35Settings** settings) noexcept
	{
		const auto& filter = pending.ladderSettings;
		korg35Settings.cutoffHz = static_cast<double>(filter.cutoffHz);
		korg35Settings.feedback = korg35Feedback(static_cast<double>(filter.resonance));
		korg35Settings.driveDecibels = static_cast<double>(filter.driveDecibels);
		korg35Settings.knee = korg35Knee;
		korg35Settings.highPass = 0.5 * (std::clamp(static_cast<double>(filter.mode), -1.0, 1.0) + 1.0);
		for (std::size_t stack = 0; stack < static_cast<std::size_t>(pending.layers); ++stack)
		{
			filters[stack] = &filterKorgs[stack];
			inputs[stack] = static_cast<double>(pending.mixers[stack]);
			settings[stack] = &korg35Settings;
		}
	}

	// After the K35 solve, given each layer's filter output: the Resonance trim, then the rest of the voice.
	void finishKorg35Sample(const double* filterOutputs, float& left, float& right) noexcept
	{
		const auto trim = korg35OutputTrim(static_cast<double>(pending.ladderSettings.resonance))
			* korg35HighPassTrim(static_cast<double>(pending.ladderSettings.mode));
		std::array<float, 4> outputs {};
		for (std::size_t stack = 0; stack < static_cast<std::size_t>(pending.layers); ++stack)
			outputs[stack] = static_cast<float>(trim * filterOutputs[stack]);
		finishSample(outputs.data(), left, right);
	}

	// Everything after the filter, given its output for each unison layer (Ladder, SVF or K35): the filter-output DC
	// blocker, amplitude, pan, layer gain and the note-transition continuity, added to left/right. The blockers carry
	// across filter type switches (a reset would itself step the output) and reset with the filters.
	void finishSample(const float* filterOutputs, float& left, float& right) noexcept
	{
		const auto unisonCount = pending.layers;
		const auto layers = static_cast<std::size_t>(unisonCount);
		const auto amplitude = pending.amplitude;
		const auto& pans = pending.pans;
		float voiceLeft {}, voiceRight {};
		for (std::size_t stack = 0; stack < layers; ++stack)
		{
			const auto stackOutput = static_cast<float>(filterDcBlockers[stack].processSample(filterOutputs[stack])) * amplitude;
			voiceLeft += stackOutput * std::sqrt(0.5f * (1.0f - pans[stack]));
			voiceRight += stackOutput * std::sqrt(0.5f * (1.0f + pans[stack]));
		}
		const auto layerGain = unisonGain(unisonCount, static_cast<float>(unisonPhaseSpread));
		voiceLeft *= layerGain;
		voiceRight *= layerGain;
		if (continuityPending)
		{
			continuityOffset = { lastOutput[0] - voiceLeft, lastOutput[1] - voiceRight };
			continuitySamples = transitionLength();
			continuityPending = false;
		}
		if (continuitySamples > 0)
		{
			const auto continuityGain = static_cast<float>(continuitySamples--) / static_cast<float>(transitionLength());
			voiceLeft += continuityOffset[0] * continuityGain;
			voiceRight += continuityOffset[1] * continuityGain;
		}
		lastOutput = { voiceLeft, voiceRight };
		left += voiceLeft;
		right += voiceRight;
		if (!amp.isActive()) active = false;
	}

	[[nodiscard]] bool isActive() const noexcept { return active; }
	[[nodiscard]] NonlinearTptLadderDiagnostics coupledDiagnostics() const noexcept;
	[[nodiscard]] NonlinearTptLadderHighPassDiagnostics ladderHighPassDiagnostics() const noexcept;
	[[nodiscard]] NonlinearTptSvfDiagnostics svfDiagnostics() const noexcept;
	[[nodiscard]] bool isHeld() const noexcept { return held; }
	[[nodiscard]] bool isSustained() const noexcept { return sustained; }
	[[nodiscard]] bool matches(int expectedChannel, int expectedNote) const noexcept { return active && channel == expectedChannel && note == expectedNote; }
	[[nodiscard]] std::uint64_t getAge() const noexcept { return age; }
	[[nodiscard]] int getChannel() const noexcept { return channel; }
	void setPanPosition(float value) noexcept { panPosition = value; }
	[[nodiscard]] float getLfoOutput(std::size_t index) const noexcept { return static_cast<float>(lfoOutputs[index]); }
	// Morph each oscillator used for the most recent sample, in [0, 4): the smoothed knob value plus LFO
	// modulation, wrapped round the cycle.
	[[nodiscard]] float getMorph(std::size_t oscillator) const noexcept { return lastMorphs[oscillator]; }
	void setPolyPressure(float pressure) noexcept { polyPressure = pressure; }
	[[nodiscard]] float getPolyPressure() const noexcept { return polyPressure; }

private:
	// A filter type change starts the newly selected filter from zero state; the other keeps its storage but is not
	// run. While the voice sounds, the continuity offset declicks the output step and the new filter's own ring-up is
	// left audible (ADR 0006); a voice starting from silence just adopts the type.
	void selectFilterType(FilterType type) noexcept
	{
		if (type == filterType) return;
		if (type == FilterType::svf) for (auto& svf : filterSvfs) svf.reset();
		else if (type == FilterType::korg35) for (auto& filter : filterKorgs) filter.reset();
		else
		{
			for (auto& ladder : filterLadders) ladder.reset();
			for (auto& filter : filterHighPasses) filter.reset();
			highPassResting = false;
			highPassSamplesAtLowPass = 0;
		}
		filterType = type;
		if (!filterRunning) return;
		continuitySamples = 0;
		continuityPending = true;
	}

	[[nodiscard]] int transitionLength() const noexcept
	{
		return std::max(1, static_cast<int>(std::round(voiceTransitionSeconds * sampleRate)));
	}

	// Envelope times scaled by this voice card's Drift tolerance (sustain is a level, left as is).
	[[nodiscard]] ContourEnvelope::Parameters ampEnvelopeParameters(const KobberVoiceSettings& settings) const noexcept
	{
		const auto scale = 1.0f + driftAmount(settings.drift) * driftEnvelopeTimeScale * driftAmpTime;
		return { settings.ampAttack * scale, settings.ampDecay * scale, settings.ampSustain, settings.ampRelease * scale };
	}

	[[nodiscard]] ContourEnvelope::Parameters filterEnvelopeParameters(const KobberVoiceSettings& settings) const noexcept
	{
		const auto scale = 1.0f + driftAmount(settings.drift) * driftEnvelopeTimeScale * driftFilterTime;
		return { settings.filterAttack * scale, settings.filterDecay * scale, settings.filterSustain, settings.filterRelease * scale };
	}

	void updateFilterControlTargets(const KobberVoiceSettings& settings)
	{
		// Cutoff reaches 5 Hz so a closed 2-pole SVF silences bass notes too (ADR 0006); modulation may take it one
		// octave further, to 2.5 Hz.
		const auto cutoffTarget = std::log2(juce::jlimit(minimumCutoffHz, absoluteMaximumCutoffHz, settings.cutoff));
		// Feedback gain k already makes the input tap inert at zero resonance.
		const auto compensationTarget = settings.qCompensation ? 0.5f : 0.0f;
		if (!filterControlsInitialized)
		{
			cutoffOctaves.setCurrentAndTargetValue(cutoffTarget);
			resonance.setCurrentAndTargetValue(settings.resonance);
			driveDecibels.setCurrentAndTargetValue(settings.drive);
			qInputCompensation.setCurrentAndTargetValue(compensationTarget);
			filterMode.setCurrentAndTargetValue(settings.filterMode);
			filterControlsInitialized = true;
			return;
		}
		if (!juce::approximatelyEqual(targetOf(cutoffOctaves), cutoffTarget)) cutoffOctaves.setTargetValue(cutoffTarget);
		if (!juce::approximatelyEqual(targetOf(resonance), settings.resonance)) resonance.setTargetValue(settings.resonance);
		if (!juce::approximatelyEqual(targetOf(driveDecibels), settings.drive)) driveDecibels.setTargetValue(settings.drive);
		if (!juce::approximatelyEqual(targetOf(qInputCompensation), compensationTarget)) qInputCompensation.setTargetValue(compensationTarget);
		if (!juce::approximatelyEqual(targetOf(filterMode), settings.filterMode)) filterMode.setTargetValue(settings.filterMode);
	}

	// The double ramps (dsp::LinearRamp) read as the float the voice works in.
	[[nodiscard]] static float nextOf(dsp::LinearRamp& ramp) noexcept { return static_cast<float>(ramp.getNextValue()); }
	[[nodiscard]] static float targetOf(const dsp::LinearRamp& ramp) noexcept { return static_cast<float>(ramp.getTargetValue()); }

	[[nodiscard]] NonlinearTptLadderSettings filterSettings(const KobberVoiceSettings& settings, float envelopeValue, float baseCutoff,
		float resonanceAmount, float driveDb, float inputCompensation, float mode, float playedNote, float lfoOctaves) const noexcept
	{
		// A ladder is controlled exponentially: keyboard, contour and velocity all
		// offset cutoff in octave/control-voltage space rather than linear Hertz.
		const auto keyOctaves = (playedNote - 60.0f) / 12.0f * settings.tracking;
		const auto contourOctaves = settings.envelopeAmount * envelopeValue * maximumContourOctaves;
		const auto velocityResponse = velocityCurve;
		const auto velocityOctaves = -settings.filterVelocity * (1.0f - velocityResponse) * maximumVelocityOctaves;
		const auto cutoff = juce::jlimit(minimumCutoffHz, maximumCutoffHz(sampleRate),
			baseCutoff * std::exp2(keyOctaves + contourOctaves + velocityOctaves + lfoOctaves));
		return { cutoff, resonanceAmount, driveDb, false, inputCompensation, mode };
	}

	[[nodiscard]] KobberModulation nextModulation(const KobberVoiceSettings& settings, const std::array<double, lfoCount>& clockPositions) noexcept
	{
		KobberModulation modulation;
		for (std::size_t index = 0; index < lfos.size(); ++index)
		{
			const auto& lfo = settings.lfo[index];
			lfos[index].setParameters(lfo.source);
			auto& smoothed = lfoOutputs[index];
			smoothed += (lfos[index].getNextSample(clockPositions[index]) - smoothed) * lfoSmoothing;
			const auto output = static_cast<float>(smoothed);
			for (std::size_t oscillator = 0; oscillator < 3; ++oscillator)
			{
				modulation.pitch[oscillator] += lfo.pitch[oscillator] * output;
				modulation.morph[oscillator] += lfo.morph[oscillator] * output;
				modulation.width[oscillator] += lfo.width[oscillator] * output;
				modulation.level[oscillator] += lfo.level[oscillator] * output;
			}
			modulation.filter += lfo.filter * output;
			modulation.amp += lfo.amp * output;
			modulation.drive += lfo.drive * output;
			modulation.noise += lfo.noise * output;
			modulation.detune += lfo.detune * output;
			modulation.spread += lfo.spread * output;
			modulation.filterMode += lfo.filterMode * output;
		}
		return modulation;
	}

	static constexpr std::array<std::uint32_t, lfoCount> lfoSharedSeeds { 0x4c464f31u, 0x4c464f32u };

	double sampleRate { 48'000.0 };
	std::array<Lfo, lfoCount> lfos;
	// Per-sample state that steps toward a target is double (ARCHITECTURE.md, DSP Contracts).
	std::array<double, lfoCount> lfoOutputs {};
	double lfoSmoothing { 1.0 }, vibratoSmoothing { 1.0 }, vibratoControl {};
	float polyPressure {};
	std::uint32_t voiceSeed {};
	int fadeInSamples {}, continuitySamples {};
	juce::Random random;
	ContourEnvelope amp, filterEnvelope;
	dsp::LinearRamp cutoffOctaves, resonance, driveDecibels, qInputCompensation, filterMode;
	std::array<dsp::LinearRamp, 3> baseMorphs, baseWidths;
	std::array<float, 3> lastMorphs {};
	bool morphsInitialized {};
	std::array<std::array<double, 3>, 4> phase {}; // [unison layer][oscillator], cycles
	std::array<NonlinearTptLadder, 4> filterLadders;
	std::array<NonlinearTptSvf, 4> filterSvfs; // side by side with the ladders: only the selected type is run
	std::array<NonlinearTptKorg35, 4> filterKorgs; // likewise
	NonlinearTptLadderSettings ladderSolveSettings; // the ladder's settings for this sample's solve (prepareLadderSolve)
	std::array<NonlinearTptLadderHighPass, 4> filterHighPasses; // the Ladder's high-pass, one per unison layer (ADR 0009)
	bool highPassResting {}; // reset after a rest at LP; primed when Mode leaves (a reset elsewhere starts cold, like the ladders)
	int highPassSamplesAtLowPass {};
	std::array<dsp::DcBlocker<double>, 4> filterDcBlockers; // per layer, after whichever filter type runs
	NonlinearTptKorg35Settings korg35Settings;             // this sample's K35 settings, shared by its layers
	FilterType filterType { FilterType::ladder };
	std::array<std::array<WidthOscillatorState, 3>, 4> widthStates {}; // [unison layer][oscillator]
	// Carried from beginSample to finishSample.
	struct PendingSample
	{
		std::array<float, 4> mixers {}, pans {};
		float amplitude {};
		int layers {};
		bool modeModulated {}; // an LFO reaches Mode (the Ladder's high-pass ladder then never rests)
		NonlinearTptLadderSettings ladderSettings {};
	} pending;
	double hostRate { 48'000.0 };
	std::array<float, 2> continuityOffset {}, lastOutput {};
	std::array<double, 4> pinkStates {}; // per layer; double, corner fixed at the host rate
	double pinkCoefficient { 0.98 };
	int noiseHoldSamples { 1 }, noiseHoldRemaining {};
	float heldSharedNoise {};
	std::array<float, 4> heldLayerNoise {};
	double unisonPhaseSpread {};
	Glide glide;
	float velocity {}, velocityCurve {}, panPosition {};
	juce::Random driftRandom;
	std::array<std::array<DriftWalk, 3>, 4> driftWalks {}; // [unison layer][oscillator]
	std::array<float, 3> driftTuning {};
	double driftCoefficient { 1.0 };
	float driftCoefficientSpeed { -1.0f };
	float driftCutoff {}, driftAmpTime {}, driftFilterTime {}, driftLevel {};
	int channel {}, note {};
	std::uint64_t age {};
	bool active {}, held {}, sustained {}, filterControlsInitialized {}, continuityPending {};
	bool filterRunning {}; // the voice has rendered since it last started from silence
	bool hasPitch {}, gliding {};
};
}
