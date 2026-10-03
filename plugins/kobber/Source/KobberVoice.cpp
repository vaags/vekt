#include "KobberVoice.h"

namespace vekt::kobber
{
void KobberVoice::prepare(double newSampleRate, std::uint32_t seed, double hostSampleRate)
{
	sampleRate = newSampleRate;
	hostRate = hostSampleRate > 0.0 ? hostSampleRate : newSampleRate;
	// Noise is drawn at the host rate and held for each internal sample, and the pink filter keeps its host-rate
	// corner, so noise level and colour match 1x at every quality (ADR 0010).
	noiseHoldSamples = std::max(1, static_cast<int>(std::lround(newSampleRate / hostRate)));
	jassert(std::abs(newSampleRate - hostRate * noiseHoldSamples) < 1.0e-6 * newSampleRate); // a whole factor
	pinkCoefficient = std::pow(0.98, 1.0 / noiseHoldSamples);
	driftCoefficientSpeed = -1.0f; // Drift walks' one-pole coefficient, recomputed when Drift's speed changes
	juce::ignoreUnused(WidthWavetable::instance()); // build the shared tables before audio starts
	for (auto& ladder : filterLadders) ladder.prepare(newSampleRate);
	for (auto& filter : filterHighPasses) filter.prepare(newSampleRate);
	for (auto& svf : filterSvfs) svf.prepare(newSampleRate);
	for (auto& filter : filterKorgs) filter.prepare(newSampleRate);
	for (auto& blocker : filterDcBlockers) blocker.prepare(newSampleRate, filterOutputDcBlockerHz);
	random.setSeed(seed);
	voiceSeed = seed;
	for (auto& lfo : lfos) lfo.setSampleRate(newSampleRate);
	// About 1 ms of smoothing on each LFO output so square and saw edges do not click.
	lfoSmoothing = 1.0 - std::exp(-1.0 / (0.001 * newSampleRate));
	// About 10 ms so the mod wheel's 128 steps do not step the vibrato depth audibly.
	vibratoSmoothing = 1.0 - std::exp(-1.0 / (0.01 * newSampleRate));
	amp.setSampleRate(newSampleRate);
	filterEnvelope.setSampleRate(newSampleRate);
	cutoffOctaves.reset(newSampleRate, 0.015);
	// Host automation and knob moves of Morph and Width ramp over 10 ms (Width moves each anchor's shape and
	// DC, so a jump would step the output); LFO modulation is added afterwards, per sample.
	for (auto& morph : baseMorphs) morph.reset(newSampleRate, 0.01);
	for (auto& width : baseWidths) width.reset(newSampleRate, 0.01);
	resonance.reset(newSampleRate, 0.02);
	driveDecibels.reset(newSampleRate, 0.02);
	qInputCompensation.reset(newSampleRate, 0.02);
	filterMode.reset(newSampleRate, 0.02);
	reset();
}

void KobberVoice::reset()
{
	// Restart the voice's random stream so everything after a reset (phases, drift, unison offsets, noise)
	// is reproducible regardless of what played before, e.g. across preset loads.
	random.setSeed(voiceSeed);
	active = held = sustained = false;
	hasPitch = gliding = false;
	amp.reset();
	filterEnvelope.reset();
	for (auto& stackPhase : phase)
		for (auto& oscillatorPhase : stackPhase) oscillatorPhase = random.nextFloat();
	// Drift: this voice card's fixed tolerances, and each oscillator's wandering pitch, from their own stream
	// so they are independent of the voice's phases and noise.
	// The voice stream once drew a static drift offset here; keep the draw so the phases and noise that
	// follow, and so every Drift-off sound, are unchanged.
	juce::ignoreUnused(random.nextFloat());
	driftRandom.setSeed(static_cast<juce::int64>(voiceSeed) * 0x9E3779B1LL + 0x5DEECE66DLL);
	for (auto& offset : driftTuning) offset = driftRandom.nextFloat() * 2.0f - 1.0f;
	driftCutoff = driftRandom.nextFloat() * 2.0f - 1.0f;
	driftAmpTime = driftRandom.nextFloat() * 2.0f - 1.0f;
	driftFilterTime = driftRandom.nextFloat() * 2.0f - 1.0f;
	driftLevel = driftRandom.nextFloat() * 2.0f - 1.0f;
	for (auto& layer : driftWalks)
		for (auto& walk : layer) walk.reset(driftRandom, sampleRate);
	pinkStates = {};
	noiseHoldRemaining = 0;
	unisonPhaseSpread = 0.0f;
	for (std::size_t index = 0; index < lfos.size(); ++index)
		lfos[index].reset(voiceSeed * 2'654'435'761u + static_cast<std::uint32_t>(index), lfoSharedSeeds[index]);
	lfoOutputs = {};
	vibratoControl = polyPressure = 0.0f;
	morphsInitialized = false;
	for (auto& ladder : filterLadders) ladder.reset();
	for (auto& filter : filterHighPasses) filter.reset();
	highPassResting = false;
	highPassSamplesAtLowPass = 0;
	for (auto& svf : filterSvfs) svf.reset();
	for (auto& filter : filterKorgs) filter.reset();
	for (auto& blocker : filterDcBlockers) blocker.reset();
	filterControlsInitialized = false;
	filterRunning = false;
	qInputCompensation.setCurrentAndTargetValue(0.0f);
	fadeInSamples = 0;
	continuitySamples = 0;
	continuityPending = false;
	continuityOffset = {};
	lastOutput = {};
}

void KobberVoice::start(int newChannel, int newNote, float newVelocity, const KobberVoiceSettings& settings, bool retrigger,
    bool legato, std::uint64_t newAge)
{
	const auto wasActive = active;
	const auto target = static_cast<float>(newNote) + settings.calibration * 0.01f;
	gliding =
	    hasPitch && (settings.glideMode == GlideMode::always || (settings.glideMode == GlideMode::legato && legato));
	if (gliding)
		glide.retarget(target);
	else
		glide.jump(target);
	hasPitch = true;
	channel = newChannel;
	note = newNote;
	velocity = newVelocity;
	age = newAge;
	velocityCurve = std::pow(juce::jlimit(0.0f, 1.0f, newVelocity), 0.65f); // shared by amp and filter velocity
	polyPressure = 0.0f;
	active = held = true;
	sustained = false;
	fadeInSamples = wasActive ? 0 : transitionLength();
	if (!wasActive) noiseHoldRemaining = 0; // a new note's noise holds start with it
	if (!wasActive)
	{
		morphsInitialized = false;
		filterRunning = false;
		// Each new note draws fresh unison phase offsets, scaled by detune: at 0 cents the layers start in
		// phase with the first (no static comb filter), and from 5 cents they are fully random. Drawing them
		// per note, not per voice slot, keeps level and tone independent of which slot plays the note.
		const auto spreadFactor = unisonSpreadFactor(settings.detune);
		unisonPhaseSpread = spreadFactor;
		for (std::size_t stack = 1; stack < phase.size(); ++stack)
			for (std::size_t oscillator = 0; oscillator < 3; ++oscillator)
			{
				auto offsetPhase = phase[0][oscillator] + spreadFactor * random.nextFloat();
				phase[stack][oscillator] = offsetPhase - std::floor(offsetPhase);
			}
	}
	continuitySamples = 0;
	continuityPending = wasActive;
	continuityOffset = {};
	if (retrigger)
	{
		amp.setParameters(ampEnvelopeParameters(settings));
		filterEnvelope.setParameters(filterEnvelopeParameters(settings));
		amp.noteOn();
		filterEnvelope.noteOn();
		// LFOs restart (Retrigger/One Shot) and re-run delay and fade whenever the envelopes retrigger.
		for (std::size_t index = 0; index < lfos.size(); ++index)
		{
			lfos[index].setParameters(settings.lfo[index].source);
			lfos[index].noteOn();
		}
	}
}

void KobberVoice::release(bool keepSustained)
{
	held = false;
	if (keepSustained)
	{
		sustained = true;
		return;
	}
	sustained = false;
	amp.noteOff();
	filterEnvelope.noteOff();
}

void KobberVoice::releaseSustain()
{
	if (sustained && !held)
	{
		sustained = false;
		amp.noteOff();
		filterEnvelope.noteOff();
	}
}

void KobberVoice::stop()
{
	active = held = sustained = false;
	hasPitch = gliding = false;
	amp.reset();
	filterEnvelope.reset();
	for (auto& ladder : filterLadders) ladder.reset();
	for (auto& filter : filterHighPasses) filter.reset();
	highPassResting = false;
	highPassSamplesAtLowPass = 0;
	for (auto& svf : filterSvfs) svf.reset();
	for (auto& filter : filterKorgs) filter.reset();
	for (auto& blocker : filterDcBlockers) blocker.reset();
	filterRunning = false;
	fadeInSamples = 0;
	continuitySamples = 0;
	continuityPending = false;
	continuityOffset = {};
	lastOutput = {};
}

NonlinearTptLadderDiagnostics KobberVoice::coupledDiagnostics() const noexcept
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

NonlinearTptLadderHighPassDiagnostics KobberVoice::ladderHighPassDiagnostics() const noexcept
{
	NonlinearTptLadderHighPassDiagnostics total;
	for (const auto& highPass : filterHighPasses)
	{
		const auto& value = highPass.diagnostics();
		total.samples += value.samples;
		total.iterations += value.iterations;
		total.unconvergedSamples += value.unconvergedSamples;
		total.nonFiniteSamples += value.nonFiniteSamples;
		total.maximumIterations = std::max(total.maximumIterations, value.maximumIterations);
	}
	return total;
}

NonlinearTptSvfDiagnostics KobberVoice::svfDiagnostics() const noexcept
{
	NonlinearTptSvfDiagnostics total;
	for (const auto& svf : filterSvfs)
	{
		const auto& value = svf.diagnostics();
		total.samples += value.samples;
		total.iterations += value.iterations;
		total.fallbackSteps += value.fallbackSteps;
		total.unconvergedSamples += value.unconvergedSamples;
		total.nonFiniteSamples += value.nonFiniteSamples;
		total.maximumIterations = std::max(total.maximumIterations, value.maximumIterations);
		total.maximumResidual = std::max(total.maximumResidual, value.maximumResidual);
	}
	return total;
}
}
