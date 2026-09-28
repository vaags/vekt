#pragma once

#include "NonlinearTptLadder.h"
#include "ContourEnvelope.h"
#include "Lfo.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace vekt::mono
{
constexpr float twoPi = 2.0f * std::numbers::pi_v<float>;
constexpr float maximumContourOctaves = 8.0f;
constexpr float maximumVelocityOctaves = 4.0f;
constexpr float voiceTransitionSeconds = 0.003f;

inline float dbToGain(float decibels) noexcept { return std::pow(10.0f, decibels / 20.0f); }
inline float midiToHz(float note) noexcept { return 440.0f * std::exp2((note - 69.0f) / 12.0f); }
// One LFO's source settings plus its per-destination depths, already scaled by Amount.
struct MonoLfoSettings
{
	Lfo::Parameters source;
	std::array<float, 3> pitch {}, morph {}, width {}, level {}; // semitones, morph units, pulse-width %, level 0..1
	float filter {}, amp {}, drive {}, noise {}, detune {}, spread {}; // octaves, gain, dB, level, cents, spread
};

inline constexpr std::size_t lfoCount = 2;

struct MonoVoiceSettings
{
	std::array<float, 3> range, semitone, fine, octave, level, morph, pulseWidth;
	float noiseLevel {}, cutoff {}, resonance {}, tracking {}, envelopeAmount {}, drive {};
	float ampAttack {}, ampDecay {}, ampSustain {}, ampRelease {};
	float filterAttack {}, filterDecay {}, filterSustain {}, filterRelease {};
	float ampVelocity {}, filterVelocity {}, calibration {};
	float detune {}, unisonSpread {}, voiceWidth {}, glideTime {}, drift {};
	int unison {}, glideMode {}, noiseType {};
	bool qCompensation {};
	std::array<MonoLfoSettings, lfoCount> lfo {};
};

// Where the LFO sum lands for one sample. Every field is an offset that is exactly zero at zero depth.
struct MonoModulation
{
	std::array<float, 3> pitch {}, morph {}, width {}, level {};
	float filter {}, amp {}, drive {}, noise {}, detune {}, spread {};
};

class MonoVoice
{
public:
	void prepare(double newSampleRate, std::uint32_t seed)
	{
		sampleRate = static_cast<float>(newSampleRate);
		for (auto& ladder : filterLadders) ladder.prepare(newSampleRate);
		random.setSeed(seed);
		lfoSeed = seed;
		for (auto& lfo : lfos) lfo.setSampleRate(newSampleRate);
		// About 1 ms of smoothing on each LFO output so square and saw edges do not click.
		lfoSmoothing = 1.0f - std::exp(-1.0f / (0.001f * static_cast<float>(newSampleRate)));
		amp.setSampleRate(newSampleRate);
		filterEnvelope.setSampleRate(newSampleRate);
		cutoffOctaves.reset(newSampleRate, 0.015);
		resonance.reset(newSampleRate, 0.02);
		driveDecibels.reset(newSampleRate, 0.02);
		qInputCompensation.reset(newSampleRate, 0.02);
		reset();
	}

	void reset()
	{
		active = held = sustained = false;
		hasPitch = gliding = false;
		amp.reset(); filterEnvelope.reset();
		for (auto& stackPhase : phase)
			for (auto& oscillatorPhase : stackPhase)
				oscillatorPhase = random.nextFloat();
		driftCents = random.nextFloat() * 2.0f - 1.0f;
		for (std::size_t index = 0; index < lfos.size(); ++index)
			lfos[index].reset(lfoSeed * 2'654'435'761u + static_cast<std::uint32_t>(index), lfoSharedSeeds[index]);
		lfoOutputs = {};
		for (auto& ladder : filterLadders) ladder.reset();
		filterControlsInitialized = false;
		qInputCompensation.setCurrentAndTargetValue(0.0f);
		fadeInSamples = 0;
		continuitySamples = 0;
		continuityPending = false;
		continuityOffset = {};
		lastOutput = {};
	}

	void start(int newChannel, int newNote, float newVelocity, const MonoVoiceSettings& settings, bool retrigger, bool legato, std::uint64_t newAge)
	{
		const auto wasActive = active;
		const auto target = static_cast<float>(newNote) + settings.calibration * 0.01f;
		gliding = hasPitch && (settings.glideMode == 1 || (settings.glideMode == 2 && legato));
		if (!gliding) currentNote = target;
		hasPitch = true;
		targetNote = target;
		channel = newChannel; note = newNote; velocity = newVelocity; age = newAge;
		active = held = true; sustained = false;
		fadeInSamples = wasActive ? 0 : transitionLength();
		continuitySamples = 0;
		continuityPending = wasActive;
		continuityOffset = {};
		if (retrigger)
		{
			ContourEnvelope::Parameters ampParameters { settings.ampAttack, settings.ampDecay, settings.ampSustain, settings.ampRelease };
			ContourEnvelope::Parameters filterParameters { settings.filterAttack, settings.filterDecay, settings.filterSustain, settings.filterRelease };
			amp.setParameters(ampParameters); filterEnvelope.setParameters(filterParameters);
			amp.noteOn(); filterEnvelope.noteOn();
			// LFOs restart (Retrigger/One Shot) and re-run delay and fade whenever the envelopes retrigger.
			for (std::size_t index = 0; index < lfos.size(); ++index)
			{
				lfos[index].setParameters(settings.lfo[index].source);
				lfos[index].noteOn();
			}
		}
	}

	void release(bool keepSustained)
	{
		held = false;
		if (keepSustained) { sustained = true; return; }
		sustained = false; amp.noteOff(); filterEnvelope.noteOff();
	}

	void releaseSustain()
	{
		if (sustained && !held) { sustained = false; amp.noteOff(); filterEnvelope.noteOff(); }
	}

	void stop()
	{
		active = held = sustained = false;
		hasPitch = gliding = false;
		amp.reset(); filterEnvelope.reset();
		for (auto& ladder : filterLadders) ladder.reset();
		fadeInSamples = 0;
		continuitySamples = 0;
		continuityPending = false;
		continuityOffset = {};
		lastOutput = {};
	}

	void render(float& left, float& right, const MonoVoiceSettings& settings, float bend,
		const std::array<double, lfoCount>& lfoClockPositions = {})
	{
		if (!active) return;
		// Refresh the parameters at each rendered segment (the processor snapshots
		// automation at MIDI/block boundaries), including while a key is held.
		amp.setParameters({ settings.ampAttack, settings.ampDecay, settings.ampSustain, settings.ampRelease });
		filterEnvelope.setParameters({ settings.filterAttack, settings.filterDecay, settings.filterSustain, settings.filterRelease });
		const auto modulation = nextModulation(settings, lfoClockPositions);
		float voiceLeft {}, voiceRight {};
		const auto glideCoefficient = !gliding || settings.glideMode == 0 || settings.glideTime <= 0.0f
			? 1.0f : 1.0f - std::exp(-1.0f / (settings.glideTime * sampleRate));
		currentNote += (targetNote - currentNote) * glideCoefficient;
		const auto playedNote = currentNote + bend;
		const auto baseHz = midiToHz(playedNote);
		const auto unisonCount = settings.unison;
		const auto filterEnvelopeValue = filterEnvelope.getNextSample();
		updateFilterControlTargets(settings);
		const auto filterCutoff = std::exp2(cutoffOctaves.getNextValue());
		const auto filterResonance = resonance.getNextValue();
		const auto filterDriveDb = juce::jlimit(0.0f, 24.0f, driveDecibels.getNextValue() + modulation.drive);
		const auto inputCompensation = qInputCompensation.getNextValue();
		// No post-ladder resonance boost: isolate the ladder's own onset.
		// Q compensation still acts only on driven input inside its feedback equation.
		const auto velocityGain = (1.0f - settings.ampVelocity) + settings.ampVelocity * std::pow(velocity, 0.65f);
		const auto allocationFade = fadeInSamples > 0
			? 1.0f - static_cast<float>(fadeInSamples--) / static_cast<float>(transitionLength())
			: 1.0f;
		const auto amplitude = amp.getNextSample() * velocityGain * allocationFade * juce::jlimit(0.0f, 1.0f, 1.0f + modulation.amp);
		const auto detune = juce::jlimit(0.0f, 50.0f, settings.detune + modulation.detune);
		const auto unisonSpread = juce::jlimit(0.0f, 1.0f, settings.unisonSpread + modulation.spread);
		const auto noiseLevel = juce::jlimit(0.0f, 1.0f, settings.noiseLevel + modulation.noise);
		std::array<float, 3> levels {}, morphs {}, widths {};
		for (std::size_t oscillator = 0; oscillator < 3; ++oscillator)
		{
			levels[oscillator] = juce::jlimit(0.0f, 1.0f, settings.level[oscillator] + modulation.level[oscillator]);
			morphs[oscillator] = juce::jlimit(0.0f, 3.0f, settings.morph[oscillator] + modulation.morph[oscillator]);
			widths[oscillator] = juce::jlimit(5.0f, 95.0f, settings.pulseWidth[oscillator] + modulation.width[oscillator]);
		}
		for (int stack = 0; stack < unisonCount; ++stack)
		{
			const auto normalizedStack = unisonCount == 1 ? 0.0f : (2.0f * static_cast<float>(stack) / static_cast<float>(unisonCount - 1) - 1.0f);
			float mixer {};
			for (int oscillator = 0; oscillator < 3; ++oscillator)
			{
				const auto octave = std::exp2(settings.range[static_cast<std::size_t>(oscillator)] - 1.0f);
				const auto cents = settings.fine[static_cast<std::size_t>(oscillator)] + normalizedStack * detune
					+ driftCents * settings.drift * 0.2f;
				const auto frequency = baseHz * octave * std::exp2(settings.octave[static_cast<std::size_t>(oscillator)]
					+ (settings.semitone[static_cast<std::size_t>(oscillator)] + cents * 0.01f + modulation.pitch[static_cast<std::size_t>(oscillator)]) / 12.0f);
				const auto phaseIncrement = frequency / sampleRate;
				auto& oscillatorPhase = phase[static_cast<std::size_t>(stack)][static_cast<std::size_t>(oscillator)];
				oscillatorPhase += phaseIncrement;
				oscillatorPhase -= std::floor(oscillatorPhase);
				mixer += waveform(oscillatorPhase, phaseIncrement, morphs[static_cast<std::size_t>(oscillator)], widths[static_cast<std::size_t>(oscillator)]) * levels[static_cast<std::size_t>(oscillator)];
			}
			if (settings.noiseType != 0)
			{
				auto noise = random.nextFloat() * 2.0f - 1.0f;
				if (settings.noiseType == 2) { pink = 0.98f * pink + 0.02f * noise; noise = pink; }
				mixer += noise * noiseLevel;
			}
			const auto stackOutput = filter(mixer, settings, filterEnvelopeValue, filterCutoff,
				filterResonance, filterDriveDb, inputCompensation, stack, playedNote, modulation.filter) * amplitude;
			const auto pan = juce::jlimit(-1.0f, 1.0f, settings.voiceWidth * panPosition
				+ normalizedStack * unisonSpread);
			voiceLeft += stackOutput * std::sqrt(0.5f * (1.0f - pan)) / static_cast<float>(unisonCount);
			voiceRight += stackOutput * std::sqrt(0.5f * (1.0f + pan)) / static_cast<float>(unisonCount);
		}
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
	[[nodiscard]] NonlinearTptLadderDiagnostics coupledDiagnostics() const noexcept
	{
		NonlinearTptLadderDiagnostics total;
		for (const auto& ladder : filterLadders)
		{
			const auto& value = ladder.diagnostics();
			total.samples += value.samples;
			total.coupledIterations += value.coupledIterations;
			total.coupledLineSearchTrials += value.coupledLineSearchTrials;
			total.unconvergedSamples += value.unconvergedSamples;
			total.nonFiniteSamples += value.nonFiniteSamples;
			total.maximumResidual = std::max(total.maximumResidual, value.maximumResidual);
		}
		return total;
	}
	[[nodiscard]] bool isHeld() const noexcept { return held; }
	[[nodiscard]] bool isSustained() const noexcept { return sustained; }
	[[nodiscard]] bool matches(int expectedChannel, int expectedNote) const noexcept { return active && channel == expectedChannel && note == expectedNote; }
	[[nodiscard]] std::uint64_t getAge() const noexcept { return age; }
	[[nodiscard]] int getChannel() const noexcept { return channel; }
	void setPanPosition(float value) noexcept { panPosition = value; }
	[[nodiscard]] float getLfoOutput(std::size_t index) const noexcept { return lfoOutputs[index]; }

private:
	[[nodiscard]] int transitionLength() const noexcept
	{
		return std::max(1, static_cast<int>(std::round(voiceTransitionSeconds * sampleRate)));
	}

	static float polyBlep(float position, float phaseIncrement) noexcept
	{
		const auto increment = juce::jlimit(1.0e-6f, 0.5f, phaseIncrement);
		if (position < increment)
		{
			const auto t = position / increment;
			return t + t - t * t - 1.0f;
		}
		if (position > 1.0f - increment)
		{
			const auto t = (position - 1.0f) / increment;
			return t * t + t + t + 1.0f;
		}
		return 0.0f;
	}

	static float waveform(float position, float phaseIncrement, float morph, float width) noexcept
	{
		const auto sine = std::sin(twoPi * position);
		const auto triangle = 1.0f - 4.0f * std::abs(position - 0.5f);
		const auto saw = 2.0f * position - 1.0f - polyBlep(position, phaseIncrement);
		const auto pulseWidth = width * 0.01f;
		const auto pulsePhase = position < pulseWidth ? position + 1.0f - pulseWidth : position - pulseWidth;
		const auto pulse = (position < pulseWidth ? 1.0f : -1.0f) + polyBlep(position, phaseIncrement) - polyBlep(pulsePhase, phaseIncrement);
		const auto segment = juce::jlimit(0, 2, static_cast<int>(morph));
		const auto fraction = morph - static_cast<float>(segment);
		const std::array<float, 4> waves { sine, triangle, saw, pulse };
		return waves[static_cast<std::size_t>(segment)] + fraction * (waves[static_cast<std::size_t>(segment + 1)] - waves[static_cast<std::size_t>(segment)]);
	}

	void updateFilterControlTargets(const MonoVoiceSettings& settings)
	{
		const auto cutoffTarget = std::log2(juce::jlimit(10.0f, 32'000.0f, settings.cutoff));
		// Feedback gain k already makes the input tap inert at zero resonance.
		const auto compensationTarget = settings.qCompensation ? 0.5f : 0.0f;
		if (!filterControlsInitialized)
		{
			cutoffOctaves.setCurrentAndTargetValue(cutoffTarget);
			resonance.setCurrentAndTargetValue(settings.resonance);
			driveDecibels.setCurrentAndTargetValue(settings.drive);
			qInputCompensation.setCurrentAndTargetValue(compensationTarget);
			filterControlsInitialized = true;
			return;
		}
		if (!juce::approximatelyEqual(cutoffOctaves.getTargetValue(), cutoffTarget)) cutoffOctaves.setTargetValue(cutoffTarget);
		if (!juce::approximatelyEqual(resonance.getTargetValue(), settings.resonance)) resonance.setTargetValue(settings.resonance);
		if (!juce::approximatelyEqual(driveDecibels.getTargetValue(), settings.drive)) driveDecibels.setTargetValue(settings.drive);
		if (!juce::approximatelyEqual(qInputCompensation.getTargetValue(), compensationTarget)) qInputCompensation.setTargetValue(compensationTarget);
	}

	float filter(float input, const MonoVoiceSettings& settings, float envelopeValue, float baseCutoff,
		float resonanceAmount, float driveDb, float inputCompensation, int stack, float playedNote, float lfoOctaves)
	{
		// A ladder is controlled exponentially: keyboard, contour and velocity all
		// offset cutoff in octave/control-voltage space rather than linear Hertz.
		const auto keyOctaves = (playedNote - 60.0f) / 12.0f * settings.tracking;
		const auto contourOctaves = settings.envelopeAmount * envelopeValue * maximumContourOctaves;
		const auto velocityResponse = std::pow(juce::jlimit(0.0f, 1.0f, velocity), 0.65f);
		const auto velocityOctaves = -settings.filterVelocity * (1.0f - velocityResponse) * maximumVelocityOctaves;
		const auto maximumCutoff = std::min(32'000.0f, sampleRate * 0.45f);
		const auto cutoff = juce::jlimit(10.0f, maximumCutoff,
			baseCutoff * std::exp2(keyOctaves + contourOctaves + velocityOctaves + lfoOctaves));
		const NonlinearTptLadderSettings ladderSettings { cutoff, resonanceAmount, driveDb, false, inputCompensation };
		return filterLadders[static_cast<std::size_t>(stack)].processCoupled(input, ladderSettings);
	}

	[[nodiscard]] MonoModulation nextModulation(const MonoVoiceSettings& settings, const std::array<double, lfoCount>& clockPositions) noexcept
	{
		MonoModulation modulation;
		for (std::size_t index = 0; index < lfos.size(); ++index)
		{
			const auto& lfo = settings.lfo[index];
			lfos[index].setParameters(lfo.source);
			auto& output = lfoOutputs[index];
			output += (lfos[index].getNextSample(clockPositions[index]) - output) * lfoSmoothing;
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
		}
		return modulation;
	}

	static constexpr std::array<std::uint32_t, lfoCount> lfoSharedSeeds { 0x4c464f31u, 0x4c464f32u };

	float sampleRate { 48'000.0f };
	std::array<Lfo, lfoCount> lfos;
	std::array<float, lfoCount> lfoOutputs {};
	float lfoSmoothing { 1.0f };
	std::uint32_t lfoSeed {};
	int fadeInSamples {}, continuitySamples {};
	juce::Random random;
	ContourEnvelope amp, filterEnvelope;
	juce::SmoothedValue<float> cutoffOctaves, resonance, driveDecibels, qInputCompensation;
	std::array<std::array<float, 3>, 4> phase {};
	std::array<NonlinearTptLadder, 4> filterLadders;
	std::array<float, 2> continuityOffset {}, lastOutput {};
	float pink {}, currentNote {}, targetNote {}, velocity {}, panPosition {}, driftCents {};
	int channel {}, note {};
	std::uint64_t age {};
	bool active {}, held {}, sustained {}, filterControlsInitialized {}, continuityPending {};
	bool hasPitch {}, gliding {};
};
}
