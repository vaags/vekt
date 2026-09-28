#pragma once

#include "NonlinearTptLadder.h"

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
};

class MonoVoice
{
public:
	void prepare(double newSampleRate, std::uint32_t seed)
	{
		sampleRate = static_cast<float>(newSampleRate);
		for (auto& ladder : filterLadders) ladder.prepare(newSampleRate);
		random.setSeed(seed);
		amp.setSampleRate(newSampleRate);
		filterEnvelope.setSampleRate(newSampleRate);
		cutoffOctaves.reset(newSampleRate, 0.015);
		resonance.reset(newSampleRate, 0.02);
		driveDecibels.reset(newSampleRate, 0.02);
		qCompensationGain.reset(newSampleRate, 0.02);
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
		for (auto& ladder : filterLadders) ladder.reset();
		filterControlsInitialized = false;
		qCompensationGain.setCurrentAndTargetValue(1.0f);
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
			juce::ADSR::Parameters ampParameters { settings.ampAttack, settings.ampDecay, settings.ampSustain, settings.ampRelease };
			juce::ADSR::Parameters filterParameters { settings.filterAttack, settings.filterDecay, settings.filterSustain, settings.filterRelease };
			amp.setParameters(ampParameters); filterEnvelope.setParameters(filterParameters);
			amp.noteOn(); filterEnvelope.noteOn();
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

	void render(float& left, float& right, const MonoVoiceSettings& settings, float bend)
	{
		if (!active) return;
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
		const auto filterDriveDb = driveDecibels.getNextValue();
		const auto outputCompensation = qCompensationGain.getNextValue();
		// Post-ladder calibration for audible free-running tone. Keep the raw
		// filter and the optional Q compensation independent of this voice gain.
		const auto onset = juce::jlimit(0.0f, 1.0f, (filterResonance - 0.98f) / 0.02f);
		const auto selfOscillationGain = 1.0f + 0.6f * onset * onset * (3.0f - 2.0f * onset);
		const auto velocityGain = (1.0f - settings.ampVelocity) + settings.ampVelocity * std::pow(velocity, 0.65f);
		const auto allocationFade = fadeInSamples > 0
			? 1.0f - static_cast<float>(fadeInSamples--) / static_cast<float>(transitionLength())
			: 1.0f;
		const auto amplitude = amp.getNextSample() * velocityGain * allocationFade;
		for (int stack = 0; stack < unisonCount; ++stack)
		{
			const auto normalizedStack = unisonCount == 1 ? 0.0f : (2.0f * static_cast<float>(stack) / static_cast<float>(unisonCount - 1) - 1.0f);
			float mixer {};
			for (int oscillator = 0; oscillator < 3; ++oscillator)
			{
				const auto octave = std::exp2(settings.range[static_cast<std::size_t>(oscillator)] - 1.0f);
				const auto cents = settings.fine[static_cast<std::size_t>(oscillator)] + normalizedStack * settings.detune
					+ driftCents * settings.drift * 0.2f;
				const auto frequency = baseHz * octave * std::exp2(settings.octave[static_cast<std::size_t>(oscillator)]
					+ (settings.semitone[static_cast<std::size_t>(oscillator)] + cents * 0.01f) / 12.0f);
				const auto phaseIncrement = frequency / sampleRate;
				auto& oscillatorPhase = phase[static_cast<std::size_t>(stack)][static_cast<std::size_t>(oscillator)];
				oscillatorPhase += phaseIncrement;
				oscillatorPhase -= std::floor(oscillatorPhase);
				mixer += waveform(oscillatorPhase, phaseIncrement, settings.morph[static_cast<std::size_t>(oscillator)], settings.pulseWidth[static_cast<std::size_t>(oscillator)]) * settings.level[static_cast<std::size_t>(oscillator)];
			}
			if (settings.noiseType != 0)
			{
				auto noise = random.nextFloat() * 2.0f - 1.0f;
				if (settings.noiseType == 2) { pink = 0.98f * pink + 0.02f * noise; noise = pink; }
				mixer += noise * settings.noiseLevel;
			}
			const auto stackOutput = filter(mixer, settings, filterEnvelopeValue, filterCutoff,
				filterResonance, filterDriveDb, stack, playedNote) * amplitude;
			const auto pan = juce::jlimit(-1.0f, 1.0f, settings.voiceWidth * panPosition
				+ normalizedStack * settings.unisonSpread);
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
		left += voiceLeft * outputCompensation * selfOscillationGain;
		right += voiceRight * outputCompensation * selfOscillationGain;
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
		const auto compensationTarget = settings.qCompensation
			? std::min(3.9810717f, 1.0f + 4.0f * std::pow(juce::jlimit(0.0f, 1.0f, settings.resonance), 0.72f))
			: 1.0f;
		if (!filterControlsInitialized)
		{
			cutoffOctaves.setCurrentAndTargetValue(cutoffTarget);
			resonance.setCurrentAndTargetValue(settings.resonance);
			driveDecibels.setCurrentAndTargetValue(settings.drive);
			qCompensationGain.setCurrentAndTargetValue(compensationTarget);
			filterControlsInitialized = true;
			return;
		}
		if (!juce::approximatelyEqual(cutoffOctaves.getTargetValue(), cutoffTarget)) cutoffOctaves.setTargetValue(cutoffTarget);
		if (!juce::approximatelyEqual(resonance.getTargetValue(), settings.resonance)) resonance.setTargetValue(settings.resonance);
		if (!juce::approximatelyEqual(driveDecibels.getTargetValue(), settings.drive)) driveDecibels.setTargetValue(settings.drive);
		if (!juce::approximatelyEqual(qCompensationGain.getTargetValue(), compensationTarget)) qCompensationGain.setTargetValue(compensationTarget);
	}

	float filter(float input, const MonoVoiceSettings& settings, float envelopeValue, float baseCutoff,
		float resonanceAmount, float driveDb, int stack, float playedNote)
	{
		// A ladder is controlled exponentially: keyboard, contour and velocity all
		// offset cutoff in octave/control-voltage space rather than linear Hertz.
		const auto keyOctaves = (playedNote - 60.0f) / 12.0f * settings.tracking;
		const auto contourOctaves = settings.envelopeAmount * envelopeValue * maximumContourOctaves;
		const auto velocityResponse = std::pow(juce::jlimit(0.0f, 1.0f, velocity), 0.65f);
		const auto velocityOctaves = -settings.filterVelocity * (1.0f - velocityResponse) * maximumVelocityOctaves;
		const auto maximumCutoff = std::min(32'000.0f, sampleRate * 0.45f);
		const auto cutoff = juce::jlimit(10.0f, maximumCutoff,
			baseCutoff * std::exp2(keyOctaves + contourOctaves + velocityOctaves));
		const NonlinearTptLadderSettings ladderSettings { cutoff, resonanceAmount, driveDb };
		return filterLadders[static_cast<std::size_t>(stack)].processCoupled(input, ladderSettings);
	}

	float sampleRate { 48'000.0f };
	int fadeInSamples {}, continuitySamples {};
	juce::Random random;
	juce::ADSR amp, filterEnvelope;
	juce::SmoothedValue<float> cutoffOctaves, resonance, driveDecibels, qCompensationGain;
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
