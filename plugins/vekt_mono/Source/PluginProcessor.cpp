#include <vekt/mono/PluginProcessor.h>

#include <vekt/mono/FactoryPresets.h>
#include "LfoDestinations.h"
#include "MonoVoice.h"
#include "MonoRenderWorkers.h"
#include <vekt/mono/PluginEditor.h>

#include <vekt/plugin_support/RequireParameter.h>
#include <vekt/presets/PresetPaths.h>
#include <vekt/presets/PresetSchema.h>

#include <algorithm>
#include <array>
#include <limits>

namespace vekt::mono
{
namespace
{
int choiceToVoiceCount(float value) noexcept
{
	constexpr std::array counts { 2, 4, 8, 12, 16 };
	return counts[static_cast<std::size_t>(juce::jlimit(0, static_cast<int>(counts.size()) - 1, juce::roundToInt(value)))];
}
int choiceToUnison(float value) noexcept { return value < 0.5f ? 1 : value < 1.5f ? 2 : 4; }
}

MonoVoiceSettings PluginProcessor::snapshotSettings() const
{
	MonoVoiceSettings settings;
	for (std::size_t index = 0; index < 3; ++index)
	{
		settings.range[index] = value(cached.range[index]);
		settings.semitone[index] = value(cached.semitone[index]);
		settings.fine[index] = value(cached.fine[index]);
		settings.octave[index] = value(cached.octave[index]);
		settings.level[index] = value(cached.level[index]) * 0.01f;
		settings.morph[index] = value(cached.morph[index]);
		settings.pulseWidth[index] = value(cached.pulseWidth[index]);
	}
	settings.noiseType = juce::roundToInt(value(cached.noiseType));
	settings.noiseLevel = value(cached.noiseLevel) * 0.01f;
	settings.cutoff = value(cached.filterCutoff);
	settings.resonance = value(cached.filterResonance) * 0.01f;
	settings.tracking = value(cached.filterKeyTracking) * 0.01f;
	settings.envelopeAmount = value(cached.filterEnvelopeAmount) * 0.01f;
	settings.drive = value(cached.filterDrive);
	settings.qCompensation = value(cached.filterQCompensation) >= 0.5f;
	settings.filterMode = value(cached.filterMode);
	constexpr std::array filterTypes { FilterType::ladder, FilterType::svf, FilterType::korg35 }; // the choices' order
	settings.filterType = filterTypes[static_cast<std::size_t>(juce::jlimit(0, 2, juce::roundToInt(value(cached.filterType))))];
	settings.ampAttack = value(cached.ampAttack);
	settings.ampDecay = value(cached.ampDecay);
	settings.ampSustain = value(cached.ampSustain) * 0.01f;
	settings.ampRelease = value(cached.ampRelease);
	settings.filterAttack = value(cached.filterAttack);
	settings.filterDecay = value(cached.filterDecay);
	settings.filterSustain = value(cached.filterSustain) * 0.01f;
	settings.filterRelease = value(cached.filterRelease);
	settings.ampVelocity = value(cached.ampVelocity) * 0.01f;
	settings.filterVelocity = value(cached.filterVelocity) * 0.01f;
	settings.calibration = value(cached.calibration);
	settings.unison = choiceToUnison(value(cached.unison));
	settings.detune = value(cached.unisonDetune);
	settings.unisonSpread = value(cached.unisonSpread) * 0.01f;
	settings.voiceWidth = value(cached.voiceWidth) * 0.01f;
	settings.drift = value(cached.drift);
	settings.glideMode = juce::roundToInt(value(cached.glideMode));
	settings.glideTime = value(cached.glideTime);
	for (std::size_t index = 0; index < parameters::lfos.size(); ++index)
	{
		const auto& ids = cached.lfos[index];
		auto& lfo = settings.lfo[index];
		const auto division = juce::roundToInt(value(ids.division));
		lfo.source.rateHz = value(ids.sync) >= 0.5f ? syncedLfoRateHz(transportBpm, division) : value(ids.rate);
		lfo.source.shape = static_cast<LfoShape>(juce::roundToInt(value(ids.shape)));
		lfo.source.polarity = static_cast<LfoPolarity>(juce::roundToInt(value(ids.polarity)));
		lfo.source.mode = static_cast<LfoMode>(juce::roundToInt(value(ids.mode)));
		lfo.source.phase = value(ids.phase) / 360.0f;
		lfo.source.delaySeconds = value(ids.delay);
		lfo.source.fadeSeconds = value(ids.fade);
		lfo.source.drift = settings.drift * 0.01f;
		// Convert each depth to its destination's own units, scaled by the master Amount. The editor's modulation
		// rings use the same table.
		const auto amount = value(ids.amount) * 0.01f;
		const auto& depths = ids.depths;
		const auto offset = [&](std::size_t destination)
		{
			return amount * value(depths[destination]) * lfoDestinations[destination].offsetPerDepth;
		};
		for (std::size_t oscillator = 0; oscillator < 3; ++oscillator)
		{
			lfo.pitch[oscillator] = offset(lfo_depth::pitch + oscillator);
			lfo.morph[oscillator] = offset(lfo_depth::morph + oscillator);
			lfo.width[oscillator] = offset(lfo_depth::width + oscillator);
			lfo.level[oscillator] = offset(lfo_depth::level + oscillator);
		}
		lfo.filter = offset(lfo_depth::filter);
		lfo.amp = offset(lfo_depth::amp);
		lfo.drive = offset(lfo_depth::drive);
		lfo.noise = offset(lfo_depth::noise);
		lfo.detune = offset(lfo_depth::detune);
		lfo.spread = offset(lfo_depth::spread);
		lfo.filterMode = offset(lfo_depth::filterMode);
	}
	return settings;
}

PluginProcessor::CachedParameters PluginProcessor::cacheParameters(juce::AudioProcessorValueTreeState& state)
{
	const auto require = [&state](const char* identifier) { return plugin_support::requireParameter(state, identifier); };
	static_assert(std::tuple_size_v<decltype(CachedParameters::lfos)> == parameters::lfos.size());
	static_assert(std::tuple_size_v<decltype(LfoParameters::depths)> == parameters::lfos[0].depths().size());
	CachedParameters result;
	const std::array oscillators {
		std::array { parameters::osc1Range, parameters::osc1Semitone, parameters::osc1Fine, parameters::osc1Octave, parameters::osc1Level, parameters::osc1Morph, parameters::osc1PulseWidth },
		std::array { parameters::osc2Range, parameters::osc2Semitone, parameters::osc2Fine, parameters::osc2Octave, parameters::osc2Level, parameters::osc2Morph, parameters::osc2PulseWidth },
		std::array { parameters::osc3Range, parameters::osc3Semitone, parameters::osc3Fine, parameters::osc3Octave, parameters::osc3Level, parameters::osc3Morph, parameters::osc3PulseWidth } };
	for (std::size_t index = 0; index < oscillators.size(); ++index)
	{
		const auto& ids = oscillators[index];
		result.range[index] = require(ids[0]);
		result.semitone[index] = require(ids[1]);
		result.fine[index] = require(ids[2]);
		result.octave[index] = require(ids[3]);
		result.level[index] = require(ids[4]);
		result.morph[index] = require(ids[5]);
		result.pulseWidth[index] = require(ids[6]);
	}
	result.noiseType = require(parameters::noiseType);
	result.noiseLevel = require(parameters::noiseLevel);
	result.filterCutoff = require(parameters::filterCutoff);
	result.filterResonance = require(parameters::filterResonance);
	result.filterKeyTracking = require(parameters::filterKeyTracking);
	result.filterEnvelopeAmount = require(parameters::filterEnvelopeAmount);
	result.filterDrive = require(parameters::filterDrive);
	result.filterQCompensation = require(parameters::filterQCompensation);
	result.filterMode = require(parameters::filterMode);
	result.filterType = require(parameters::filterType);
	result.ampAttack = require(parameters::ampAttack);
	result.ampDecay = require(parameters::ampDecay);
	result.ampSustain = require(parameters::ampSustain);
	result.ampRelease = require(parameters::ampRelease);
	result.filterAttack = require(parameters::filterAttack);
	result.filterDecay = require(parameters::filterDecay);
	result.filterSustain = require(parameters::filterSustain);
	result.filterRelease = require(parameters::filterRelease);
	result.ampVelocity = require(parameters::ampVelocity);
	result.filterVelocity = require(parameters::filterVelocity);
	result.calibration = require(parameters::calibration);
	result.unison = require(parameters::unison);
	result.unisonDetune = require(parameters::unisonDetune);
	result.unisonSpread = require(parameters::unisonSpread);
	result.voiceWidth = require(parameters::voiceWidth);
	result.drift = require(parameters::drift);
	result.glideMode = require(parameters::glideMode);
	result.glideTime = require(parameters::glideTime);
	result.multicore = require(parameters::multicore);
	result.voiceCount = require(parameters::voiceCount);
	result.pitchBendRange = require(parameters::pitchBendRange);
	result.performanceMode = require(parameters::performanceMode);
	result.notePriority = require(parameters::notePriority);
	result.heldKeyReturn = require(parameters::heldKeyReturn);
	result.vibratoRate = require(parameters::vibratoRate);
	result.vibratoShape = require(parameters::vibratoShape);
	result.vibratoDepth = require(parameters::vibratoDepth);
	result.vibratoAmount = require(parameters::vibratoAmount);
	result.masterOutput = require(parameters::masterOutput);
	for (std::size_t index = 0; index < parameters::lfos.size(); ++index)
	{
		const auto& ids = parameters::lfos[index];
		auto& lfo = result.lfos[index];
		lfo = { require(ids.rate), require(ids.sync), require(ids.division), require(ids.shape), require(ids.polarity),
			require(ids.mode), require(ids.phase), require(ids.delay), require(ids.fade), require(ids.amount) };
		const auto depths = ids.depths();
		for (std::size_t destination = 0; destination < depths.size(); ++destination)
			lfo.depths[destination] = require(depths[destination]);
	}
	return result;
}

PluginProcessor::PluginProcessor()
	: AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
	  parameterState(*this, &undoManager, parameters::stateType, parameters::createLayout()),
	  presetHost(parameterState, { parameters::presetProductIdentifier, "Vekt Mono", parameters::presetSoundSchemaVersion }, {
		[this](const juce::String& name)
		{
			auto preset = presets::PresetSchema::create(parameters::presetProductIdentifier, name, parameterState, parameters::soundParameterIds);
			preset.soundSchemaVersion = parameters::presetSoundSchemaVersion;
			return preset;
		},
		[](presets::Preset& preset)
		{
			return preset.soundSchemaVersion == parameters::presetSoundSchemaVersion ? juce::Result::ok() : juce::Result::fail("Unsupported Mono preset sound schema");
		},
		[this](const presets::Preset& preset) { return validatePresetSound(preset); },
		[this](const presets::Preset& preset) { return applyPreset(preset); },
		[this](const presets::Preset& preset) { return matchesPresetSound(preset); } }),
	  qualitySelection(parameterState, parameters::trackingOversampling, parameters::offlineOversampling),
	  cached(cacheParameters(parameterState))
{
	for (std::size_t index = 0; index < voices.size(); ++index)
		voices[index] = std::make_unique<MonoVoice>();
	for (auto& clock : lfoClocks) clock = std::make_unique<LfoClock>();
	vibratoClock = std::make_unique<LfoClock>();
	for (auto& heldNotes : heldNotesByChannel)
		heldNotes.reserve(128);
	const auto factoryResult = addFactoryPresets(presetHost.catalog());
	jassert(factoryResult.wasOk());
	juce::ignoreUnused(factoryResult);
	juce::ignoreUnused(presetHost.configureUserPresetDirectory(presets::PresetPaths::desktop("Vekt Mono")));
	presets::Preset initialPreset;
	if (presetHost.catalog().loadFactoryPreset(0, initialPreset).wasOk()
		&& presetHost.session().prepare(initialPreset).wasOk()
		&& presets::PresetSchema::apply(initialPreset, parameters::presetProductIdentifier, parameterState, parameters::soundParameterIds).wasOk())
		presetHost.session().adopt(initialPreset, presets::PresetOrigin::factory);
	workgroupMailbox = std::make_unique<WorkgroupMailbox>();
	parameterState.addParameterListener(parameters::multicore, this);
}

PluginProcessor::~PluginProcessor()
{
	parameterState.removeParameterListener(parameters::multicore, this);
	cancelPendingUpdate();
	renderWorkers.store(nullptr);
	renderWorkerPool.reset();
}

void PluginProcessor::parameterChanged(const juce::String& identifier, float newValue)
{
	if (identifier != parameters::multicore || newValue < 0.5f) return;
	// Threads are created off the audio thread: now if this is the message thread, else asynchronously.
	if (juce::MessageManager::existsAndIsCurrentThread()) ensureRenderWorkers();
	else triggerAsyncUpdate();
}

void PluginProcessor::handleAsyncUpdate() { ensureRenderWorkers(); }

void PluginProcessor::ensureRenderWorkers()
{
	// prepareToPlay (a host thread) and the message thread can both get here; never the audio thread.
	const std::scoped_lock lock(renderWorkerCreation);
	if (renderWorkerPool != nullptr) return;
	const auto helpers = MonoRenderWorkers::defaultThreadCount();
	if (helpers <= 0) return;
	// The helpers join whatever workgroup the mailbox holds when they first wake.
	renderWorkerPool = std::make_unique<MonoRenderWorkers>(helpers, helperBlockSize.load(), helperSampleRate.load(), *workgroupMailbox);
	renderWorkers.store(renderWorkerPool.get(), std::memory_order_release);
}

void PluginProcessor::audioWorkgroupContextChanged(const juce::AudioWorkgroup& workgroup)
{
	// JUCE calls this from the audio/render callback, which must not wait for a helper reading the mailbox.
	stagedWorkgroup = workgroup;
	workgroupStaged = !workgroupMailbox->tryPublish(stagedWorkgroup);
}

int PluginProcessor::getRenderHelperCount() const noexcept
{
	const auto* workers = renderWorkers.load(std::memory_order_acquire);
	return workers != nullptr ? workers->threads() : 0;
}

PluginProcessor::CoupledWorkSnapshot PluginProcessor::coupledWorkSnapshot() const noexcept
{
	CoupledWorkSnapshot total;
	for (const auto& voice : voices)
	{
		const auto value = voice->coupledDiagnostics();
		total.samples += value.samples;
		total.iterations += value.coupledIterations;
		total.lineSearchTrials += value.coupledLineSearchTrials;
		total.unconverged += value.unconvergedSamples;
		total.nonFinite += value.nonFiniteSamples;
	}
	return total;
}

PluginProcessor::SvfWorkSnapshot PluginProcessor::svfWorkSnapshot() const noexcept
{
	SvfWorkSnapshot total;
	for (const auto& voice : voices)
	{
		const auto value = voice->svfDiagnostics();
		total.samples += value.samples;
		total.iterations += value.iterations;
		total.fallbackSteps += value.fallbackSteps;
		total.unconverged += value.unconvergedSamples;
		total.nonFinite += value.nonFiniteSamples;
		total.maximumIterations = std::max(total.maximumIterations, value.maximumIterations);
		total.maximumResidual = std::max(total.maximumResidual, value.maximumResidual);
	}
	return total;
}

void PluginProcessor::prepareToPlay(double newSampleRate, int maximumBlockSize)
{
	sampleRateHz = newSampleRate;
	outputScope.prepare(newSampleRate);
	// The display timeline restarts with the count (DisplayTimeline sees it go back).
	renderedSamples = 0;
	preparedBlockSize = std::max(maximumBlockSize, 1);
	helperBlockSize.store(std::max(preparedBlockSize, 64));
	helperSampleRate.store(newSampleRate);
	oversampling.prepare(static_cast<std::size_t>(preparedBlockSize));
	// Segment buffers at the highest internal rate, so no allocation happens while rendering.
	voiceStride = static_cast<std::size_t>(preparedBlockSize) * oversampling.getMaximumFactor();
	lfoPositionBuffer.assign(2 * voiceStride, 0.0);
	vibratoBuffer.assign(voiceStride, 0.0f);
	voiceBuffer.assign(voices.size() * 2 * voiceStride, 0.0f);
	if (value(cached.multicore) >= 0.5f) ensureRenderWorkers();
	for (auto& clock : lfoClocks) clock->reset();
	vibratoClock->reset();
	activeVoiceCount = choiceToVoiceCount(value(cached.voiceCount));
	configureQuality(qualitySelection.prepare(isNonRealtime()));
}

void PluginProcessor::releaseResources()
{
	qualitySelection.release();
	resetPlayingState();
	soundingVoiceDisplay.store(0, std::memory_order_relaxed);
	lfoDisplayActive.store(false, std::memory_order_relaxed);
	outputMeter.reset();
}
bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const { return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo(); }

int PluginProcessor::activeVoiceLimit() const noexcept { return activeVoiceCount; }
int PluginProcessor::getSoundingVoiceCount() const noexcept
{
	return static_cast<int>(std::count_if(voices.begin(), voices.end(), [](const auto& voice) { return voice->isActive(); }));
}

void PluginProcessor::applyConfigurationChanges()
{
	const auto voiceCount = choiceToVoiceCount(value(cached.voiceCount));
	const auto quality = qualitySelection.takeRequest(isNonRealtime());
	if (voiceCount == activeVoiceCount && !quality) return;
	// Voice count and quality apply at once and cut whatever is sounding (ADR 0010).
	resetPlayingState();
	oversampling.reset();
	activeVoiceCount = voiceCount;
	if (quality) configureQuality(*quality);
}

void PluginProcessor::configureQuality(dsp::OversamplingQuality quality)
{
	oversampling.activate(quality);
	const auto effectiveSampleRate = sampleRateHz * static_cast<double>(oversampling.getActiveFactor());
	for (auto& clock : lfoClocks) clock->setSampleRate(effectiveSampleRate);
	vibratoClock->setSampleRate(effectiveSampleRate);
	for (std::size_t index = 0; index < voices.size(); ++index)
	{
		voices[index]->prepare(effectiveSampleRate,
			0x4d6f6e6fu + static_cast<std::uint32_t>(index * 977), sampleRateHz);
	}
	setLatencySamples(oversampling.getActiveLatencySamples());
	latencyDisplay.store(oversampling.getActiveLatencySamples(), std::memory_order_relaxed);
}

void PluginProcessor::renderJobCallback(void* processor, int job) noexcept
{
	static_cast<PluginProcessor*>(processor)->renderJob(job);
}

void PluginProcessor::renderJob(int job) noexcept
{
	const auto& settings = *segment.settings;
	const auto& jobVoices = segment.jobVoices[static_cast<std::size_t>(job)];
	const auto voiceCount = static_cast<std::size_t>(segment.jobVoiceCount[static_cast<std::size_t>(job)]);
	std::array<float*, 4> output {}; // each voice's left samples, its right one stride later
	for (std::size_t index = 0; index < voiceCount; ++index)
		output[index] = voiceBuffer.data() + static_cast<std::size_t>(2 * jobVoices[index]) * voiceStride;
	// The filter type is fixed for the segment: choose the per-sample filter step once, not per sample.
	const auto renderSamples = [&](auto&& filterVoices)
	{
		for (int sample = 0; sample < segment.samples; ++sample)
		{
			const std::array lfoPositions { lfoPositionBuffer[2 * static_cast<std::size_t>(sample)],
				lfoPositionBuffer[2 * static_cast<std::size_t>(sample) + 1] };
			const auto vibrato = vibratoBuffer[static_cast<std::size_t>(sample)];
			const auto beginVoice = [&](MonoVoice& voice)
			{
				const auto channelIndex = static_cast<std::size_t>(juce::jlimit(0, 15, voice.getChannel() - 1));
				return voice.beginSample(settings, pitchBendByChannel[channelIndex], lfoPositions, vibrato,
					segment.channelControl[channelIndex]);
			};
			// Each voice adds one sample to its own (zeroed) pair; a silent voice leaves zeros, which mixing ignores.
			std::array<float, 4> voiceLeft {}, voiceRight {};
			filterVoices(beginVoice, voiceLeft, voiceRight);
			for (std::size_t index = 0; index < voiceCount; ++index)
			{
				output[index][sample] = voiceLeft[index];
				output[index][voiceStride + static_cast<std::size_t>(sample)] = voiceRight[index];
			}
		}
	};
	if (settings.filterType == FilterType::korg35)
	{
		renderSamples([&](const auto& beginVoice, auto& voiceLeft, auto& voiceRight)
		{
			// As the ladder: the job's voices build their K35 inputs, share one batched solve (tanh vectorized across
			// up to four lanes), then finish their samples.
			std::array<NonlinearTptKorg35*, 4> filters {};
			std::array<const NonlinearTptKorg35Settings*, 4> filterSettings {};
			std::array<double, 4> filterInputs {}, filterOutputs {};
			std::array<std::size_t, 4> firstLane {};
			std::array<bool, 4> sounding {};
			std::size_t lanes {};
			for (std::size_t index = 0; index < voiceCount; ++index)
			{
				auto& voice = *voices[jobVoices[index]];
				sounding[index] = beginVoice(voice);
				if (!sounding[index]) continue;
				firstLane[index] = lanes;
				voice.korg35Request(filters.data() + lanes, filterInputs.data() + lanes, filterSettings.data() + lanes);
				lanes += static_cast<std::size_t>(voice.korg35Lanes());
			}
			NonlinearTptKorg35::processLanes(std::span<NonlinearTptKorg35* const>(filters.data(), lanes),
				std::span<const double>(filterInputs.data(), lanes), std::span(filterOutputs.data(), lanes),
				std::span<const NonlinearTptKorg35Settings* const>(filterSettings.data(), lanes));
			for (std::size_t index = 0; index < voiceCount; ++index)
				if (sounding[index])
					voices[jobVoices[index]]->finishKorg35Sample(filterOutputs.data() + firstLane[index], voiceLeft[index], voiceRight[index]);
		});
		return;
	}
	if (settings.filterType != FilterType::ladder)
	{
		renderSamples([&](const auto& beginVoice, auto& voiceLeft, auto& voiceRight)
		{
			for (std::size_t index = 0; index < voiceCount; ++index)
			{
				auto& voice = *voices[jobVoices[index]];
				if (beginVoice(voice)) voice.finishNonLadderSample(voiceLeft[index], voiceRight[index]);
			}
		});
		return;
	}
	renderSamples([&](const auto& beginVoice, auto& voiceLeft, auto& voiceRight)
	{
		// The job's voices build their ladder inputs, share one batched solve (tanh vectorized across up to four
		// lanes), then finish their samples.
		std::array<NonlinearTptLadder*, 4> ladders {};
		std::array<const NonlinearTptLadderSettings*, 4> ladderSettings {};
		std::array<float, 4> ladderInputs {}, ladderOutputs {};
		std::array<std::size_t, 4> firstLane {};
		std::array<bool, 4> sounding {};
		std::size_t lanes {};
		for (std::size_t index = 0; index < voiceCount; ++index)
		{
			auto& voice = *voices[jobVoices[index]];
			sounding[index] = beginVoice(voice);
			if (!sounding[index]) continue;
			firstLane[index] = lanes;
			voice.ladderRequest(ladders.data() + lanes, ladderInputs.data() + lanes, ladderSettings.data() + lanes);
			lanes += static_cast<std::size_t>(voice.ladderLanes());
		}
		NonlinearTptLadder::processCoupled(std::span<NonlinearTptLadder* const>(ladders.data(), lanes),
			std::span<const float>(ladderInputs.data(), lanes), std::span(ladderOutputs.data(), lanes),
			std::span<const NonlinearTptLadderSettings* const>(ladderSettings.data(), lanes));
		for (std::size_t index = 0; index < voiceCount; ++index)
			if (sounding[index]) voices[jobVoices[index]]->finishLadderSample(ladderOutputs.data() + firstLane[index], voiceLeft[index], voiceRight[index]);
	});
}

void PluginProcessor::readTransport()
{
	transportPpq.reset();
	if (const auto* playHead = getPlayHead())
		if (const auto position = playHead->getPosition())
		{
			if (const auto bpm = position->getBpm(); bpm && *bpm > 0.0) transportBpm = *bpm;
			if (const auto ppq = position->getPpqPosition(); ppq && position->getIsPlaying()) transportPpq = *ppq;
		}
	// While the host plays, a synced Free LFO follows the song position, so it lands on the grid on every playback.
	if (!transportPpq) return;
	for (std::size_t index = 0; index < parameters::lfos.size(); ++index)
	{
		const auto& ids = cached.lfos[index];
		if (value(ids.sync) >= 0.5f)
			lfoClocks[index]->setPosition(*transportPpq / lfoDivisionBeats(juce::roundToInt(value(ids.division))));
	}
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
	juce::ScopedNoDenormals noDenormals;
	buffer.clear();
	if (pendingPresetReset.exchange(false))
	{
		resetPlayingState();
		// Preset loads must not leak old-patch samples from the oversampling filters.
		// Reset before handling MIDI so a new note can sound in this callback.
		oversampling.reset();
	}
	applyConfigurationChanges();
	readTransport();
	if (workgroupStaged) workgroupStaged = !workgroupMailbox->tryPublish(stagedWorkgroup);
	int position {};
	for (const auto metadata : midi)
	{
		const auto eventPosition = juce::jlimit(0, buffer.getNumSamples(), metadata.samplePosition);
		render(buffer, position, eventPosition - position);
		handleMidi(metadata.getMessage());
		position = eventPosition;
	}
	render(buffer, position, buffer.getNumSamples() - position);
	outputMeter.publish(buffer);
	outputScope.publish(buffer);
	const MonoVoice* newest {};
	for (const auto& voice : voices)
		if (voice->isActive() && (newest == nullptr || voice->getAge() > newest->getAge())) newest = voice.get();
	for (std::size_t index = 0; index < lfoDisplayValues.size(); ++index)
		lfoDisplayValues[index].store(newest != nullptr ? newest->getLfoOutput(index) : 0.0f, std::memory_order_relaxed);
	lfoDisplayActive.store(newest != nullptr, std::memory_order_relaxed);
	renderedSamples += static_cast<std::uint64_t>(buffer.getNumSamples());
	LfoHistory::Frame frame;
	frame.sample = renderedSamples;
	frame.sampleRate = sampleRateHz;
	// Tag by voice so the display does not blend across a change of voice; age + 1 keeps 0 for silence.
	frame.tag = newest != nullptr ? newest->getAge() + 1 : 0;
	for (std::size_t index = 0; index < frame.values.size(); ++index)
		frame.values[index] = newest != nullptr ? newest->getLfoOutput(index) : 0.0f;
	lfoHistory.publish(frame);
	auto control = std::max({ *std::max_element(modWheelByChannel.begin(), modWheelByChannel.end()),
		*std::max_element(pressureByChannel.begin(), pressureByChannel.end()), value(cached.vibratoAmount) * 0.01f });
	for (const auto& voice : voices) if (voice->isActive()) control = std::max(control, voice->getPolyPressure());
	vibratoControlDisplay.store(control, std::memory_order_relaxed);
	soundingVoiceDisplay.store(getSoundingVoiceCount(), std::memory_order_relaxed);
}

void PluginProcessor::handleMidi(const juce::MidiMessage& message)
{
	if (message.isNoteOn()) noteOn(message.getChannel(), message.getNoteNumber(), message.getFloatVelocity());
	else if (message.isNoteOff()) noteOff(message.getChannel(), message.getNoteNumber());
	else if (message.isAllSoundOff()) allNotesOff(message.getChannel(), true);
	else if (message.isAllNotesOff()) allNotesOff(message.getChannel(), false);
	else if (message.isResetAllControllers())
	{
		const auto channelIndex = static_cast<std::size_t>(message.getChannel() - 1);
		const auto sustainWasDown = sustainByChannel[channelIndex];
		sustainByChannel[channelIndex] = false;
		pitchBendByChannel[channelIndex] = 0.0f;
		modWheelByChannel[channelIndex] = pressureByChannel[channelIndex] = 0.0f;
		for (auto& voice : voices) if (voice->getChannel() == message.getChannel()) voice->setPolyPressure(0.0f);
		if (sustainWasDown) releaseSustainedNotes(message.getChannel());
	}
	else if (message.isController() && message.getControllerNumber() == 1)
		modWheelByChannel[static_cast<std::size_t>(message.getChannel() - 1)] = static_cast<float>(message.getControllerValue()) / 127.0f;
	else if (message.isChannelPressure())
		pressureByChannel[static_cast<std::size_t>(message.getChannel() - 1)] = static_cast<float>(message.getChannelPressureValue()) / 127.0f;
	else if (message.isAftertouch())
	{
		for (auto& voice : voices)
			if (voice->matches(message.getChannel(), message.getNoteNumber()))
				voice->setPolyPressure(static_cast<float>(message.getAfterTouchValue()) / 127.0f);
	}
	else if (message.isPitchWheel())
		pitchBendByChannel[static_cast<std::size_t>(message.getChannel() - 1)]
			= (static_cast<float>(message.getPitchWheelValue()) - 8192.0f) / 8192.0f * value(cached.pitchBendRange);
	else if (message.isController() && message.getControllerNumber() == 64)
	{
		auto& sustain = sustainByChannel[static_cast<std::size_t>(message.getChannel() - 1)];
		const auto wasDown = sustain; sustain = message.getControllerValue() >= 64;
		if (wasDown && !sustain) releaseSustainedNotes(message.getChannel());
	}
}

MonoVoice& PluginProcessor::findVoiceForNote(int, int)
{
	for (int index = 0; index < activeVoiceLimit(); ++index) if (!voices[static_cast<std::size_t>(index)]->isActive()) return *voices[static_cast<std::size_t>(index)];
	auto* selected = voices.front().get();
	for (int index = 0; index < activeVoiceLimit(); ++index)
	{
		auto* candidate = voices[static_cast<std::size_t>(index)].get();
		if ((!candidate->isHeld() && selected->isHeld()) || (candidate->isHeld() == selected->isHeld() && candidate->getAge() < selected->getAge())) selected = candidate;
	}
	return *selected;
}

MonoVoice& PluginProcessor::monoVoiceForChannel(int channel)
{
	return *voices[static_cast<std::size_t>(juce::jlimit(0, static_cast<int>(voices.size()) - 1, channel - 1))];
}

void PluginProcessor::noteOn(int channel, int note, float velocity)
{
	const auto settings = snapshotSettings();
	const auto mode = juce::roundToInt(value(cached.performanceMode));
	if (mode != 0)
	{
		auto& heldNotes = heldNotesByChannel[static_cast<std::size_t>(channel - 1)];
		const auto legato = !heldNotes.empty();
		heldNotes.erase(std::remove_if(heldNotes.begin(), heldNotes.end(), [note](const auto& heldNote) { return heldNote.note == note; }), heldNotes.end());
		heldNotes.push_back({ note, velocity });
		const auto lowPriority = value(cached.notePriority) >= 0.5f;
		const auto& selected = lowPriority ? *std::min_element(heldNotes.begin(), heldNotes.end(), [](const auto& a, const auto& b) { return a.note < b.note; }) : heldNotes.back();
		if (lowPriority && selected.note != note) return;
		auto& voice = monoVoiceForChannel(channel);
		voice.setPanPosition(0.0f);
		const auto retrigger = mode == 1 || !legato || !voice.isActive() || (!voice.isHeld() && !voice.isSustained());
		voice.start(channel, note, velocity, settings, retrigger, legato, ++noteAge);
		return;
	}
	// A key that is still sounding (held, sustained or releasing) retriggers its own voice, as on analog
	// polysynths: its envelopes restart from their current level instead of stacking a second copy.
	for (auto& sounding : voices)
		if (sounding->matches(channel, note))
		{
			sounding->start(channel, note, velocity, settings, true, false, ++noteAge);
			return;
		}
	auto& voice = findVoiceForNote(channel, note);
	voice.setPanPosition(activeVoiceLimit() <= 1 ? 0.0f : 2.0f * static_cast<float>(noteAge % static_cast<std::uint64_t>(activeVoiceLimit())) / static_cast<float>(activeVoiceLimit() - 1) - 1.0f);
	voice.start(channel, note, velocity, settings, true, false, ++noteAge);
}

void PluginProcessor::noteOff(int channel, int note)
{
	const auto mode = juce::roundToInt(value(cached.performanceMode));
	if (mode != 0)
	{
		auto& heldNotes = heldNotesByChannel[static_cast<std::size_t>(channel - 1)];
		auto& voice = monoVoiceForChannel(channel);
		const auto wasActive = voice.matches(channel, note);
		heldNotes.erase(std::remove_if(heldNotes.begin(), heldNotes.end(), [note](const auto& heldNote) { return heldNote.note == note; }), heldNotes.end());
		if (wasActive && value(cached.heldKeyReturn) >= 0.5f && !heldNotes.empty())
		{
			retargetMonophonicVoice(channel, mode == 1);
			return;
		}
		if (wasActive)
			voice.release(sustainByChannel[static_cast<std::size_t>(channel - 1)]);
		return;
	}
	for (auto& voice : voices) if (voice->matches(channel, note)) voice->release(sustainByChannel[static_cast<std::size_t>(channel - 1)]);
}

void PluginProcessor::allNotesOff(int channel, bool immediate)
{
	const auto channelIndex = static_cast<std::size_t>(channel - 1);
	heldNotesByChannel[channelIndex].clear();
	sustainByChannel[channelIndex] = false;
	for (auto& voice : voices)
		if (voice->getChannel() == channel)
		{
			if (immediate) voice->stop();
			else voice->release(false);
		}
}

void PluginProcessor::retargetMonophonicVoice(int channel, bool retrigger)
{
	const auto& heldNotes = heldNotesByChannel[static_cast<std::size_t>(channel - 1)];
	if (heldNotes.empty()) return;
	const auto settings = snapshotSettings();
	const auto& returned = value(cached.notePriority) >= 0.5f
		? *std::min_element(heldNotes.begin(), heldNotes.end(), [](const auto& a, const auto& b) { return a.note < b.note; })
		: heldNotes.back();
	monoVoiceForChannel(channel).start(channel, returned.note, returned.velocity, settings, retrigger, true, ++noteAge);
}

void PluginProcessor::releaseSustainedNotes(int channel)
{
	for (auto& voice : voices)
		if (voice->getChannel() == channel)
			voice->releaseSustain();
}

void PluginProcessor::resetPlayingState()
{
	// Restart the shared modulation clocks with the voices, so output after a preset load does not depend on
	// how long the previous patch ran. A synced Free LFO then re-locks to the host position while it plays.
	for (auto& clock : lfoClocks) clock->reset();
	vibratoClock->reset();
	for (auto& voice : voices) voice->reset();
	for (auto& heldNotes : heldNotesByChannel) heldNotes.clear();
	sustainByChannel.fill(false);
	noteAge = 0;
}

void PluginProcessor::render(juce::AudioBuffer<float>& buffer, int start, int count)
{
	if (count <= 0) return;
	// The oversampler and the segment buffers only hold the prepared block size; split larger host blocks.
	if (count > preparedBlockSize)
	{
		for (int offset = 0; offset < count; offset += preparedBlockSize)
			render(buffer, start + offset, std::min(preparedBlockSize, count - offset));
		return;
	}
	const auto settings = snapshotSettings();
	for (std::size_t index = 0; index < lfoClocks.size(); ++index)
	{
		lfoClocks[index]->setRate(settings.lfo[index].source.rateHz);
		lfoDisplayRates[index].store(std::clamp(settings.lfo[index].source.rateHz, minimumLfoRateHz, maximumLfoRateHz), std::memory_order_relaxed);
	}
	vibratoClock->setRate(value(cached.vibratoRate));
	const auto vibratoShape = juce::roundToInt(value(cached.vibratoShape)) == 1 ? LfoShape::triangle : LfoShape::sine;
	const auto vibratoDepthSemitones = value(cached.vibratoDepth) * 0.01f;
	// The on-screen wheel (Vibrato Amount) plays every channel; the higher of it and each channel's controllers wins.
	const auto screenWheel = value(cached.vibratoAmount) * 0.01f;
	std::array<float, 16> channelControl {};
	for (std::size_t channel = 0; channel < channelControl.size(); ++channel)
		channelControl[channel] = std::max({ modWheelByChannel[channel], pressureByChannel[channel], screenWheel });
	const auto outputGain = dbToGain(value(cached.masterOutput));
	juce::dsp::AudioBlock<float> outputBlock(buffer);
	auto renderBlock = outputBlock.getSubBlock(static_cast<std::size_t>(start), static_cast<std::size_t>(count));
	if (oversampling.getActiveFactor() > 1)
	{
		const juce::dsp::AudioBlock<const float> inputBlock(renderBlock);
		renderBlock = oversampling.processSamplesUp(inputBlock);
	}
	const auto samples = static_cast<int>(renderBlock.getNumSamples());
	// The shared clocks advance once per sample for all voices; precompute them for the segment.
	for (int sample = 0; sample < samples; ++sample)
	{
		lfoPositionBuffer[2 * static_cast<std::size_t>(sample)] = lfoClocks[0]->getPosition();
		lfoPositionBuffer[2 * static_cast<std::size_t>(sample) + 1] = lfoClocks[1]->getPosition();
		vibratoBuffer[static_cast<std::size_t>(sample)] = Lfo::bipolarShape(vibratoShape, vibratoClock->getPosition(), 0) * vibratoDepthSemitones;
		for (auto& clock : lfoClocks) clock->advance();
		vibratoClock->advance();
	}
	// Units: sounding voices in voice order, four filter lanes each (four, two or one voice at unison 1, 2 or 4).
	// Notes only start at segment boundaries, so the units are fixed for the segment. Units fix the summing order
	// below; they do not depend on threads, and a voice renders the same bits in any job (the batched solves
	// share only tanh, which is per lane), so Multicore on and off render identical samples.
	segment.settings = &settings;
	segment.channelControl = channelControl;
	segment.samples = samples;
	const auto lanesPerVoice = std::clamp(settings.unison, 1, 4);
	const auto groupVoices = [this](int voicesPerGroup, RenderSegment::VoiceGroups& groups, std::array<int, 16>& groupVoiceCount)
	{
		auto groupCount = 0;
		for (std::size_t index = 0; index < voices.size(); ++index)
		{
			if (!voices[index]->isActive()) continue;
			if (groupCount == 0 || groupVoiceCount[static_cast<std::size_t>(groupCount - 1)] == voicesPerGroup)
				groupVoiceCount[static_cast<std::size_t>(groupCount++)] = 0;
			auto& groupSize = groupVoiceCount[static_cast<std::size_t>(groupCount - 1)];
			groups[static_cast<std::size_t>(groupCount - 1)][static_cast<std::size_t>(groupSize++)] = static_cast<std::uint8_t>(index);
		}
		return groupCount;
	};
	segment.units = groupVoices(4 / lanesPerVoice, segment.unitVoices, segment.unitVoiceCount);
	// Threads only pay off with enough samples to amortize waking the helpers.
	auto* workers = renderWorkers.load(std::memory_order_acquire);
	const auto threads = workers != nullptr && value(cached.multicore) >= 0.5f && samples >= 32 ? workers->threads() + 1 : 1;
	segment.jobs = segment.units;
	segment.jobVoices = segment.unitVoices;
	segment.jobVoiceCount = segment.unitVoiceCount;
	if (threads > 1 && segment.units < threads && lanesPerVoice < 4)
	{
		// Fewer units than threads: render in smaller jobs. A smaller batched solve costs more per lane but less in
		// total, so pick the job size with the shortest estimated wall time (rounds of jobs across the threads).
		// Relative cost of a job of 4, 2 and 1 filter lanes: 16 ladder voices at 1x and 4x (VektMonoProcessorCost).
		constexpr std::array<std::pair<int, float>, 3> jobCosts { { { 4, 1.0f }, { 2, 0.53f }, { 1, 0.30f } } };
		const auto sounding = static_cast<int>(std::count_if(voices.begin(), voices.end(), [](const auto& voice) { return voice->isActive(); }));
		auto bestLanes = 4;
		auto bestTime = std::numeric_limits<float>::max();
		for (const auto& [lanes, cost] : jobCosts)
		{
			if (lanes < lanesPerVoice) continue;
			const auto voicesPerJob = lanes / lanesPerVoice;
			const auto jobs = (sounding + voicesPerJob - 1) / voicesPerJob;
			const int rounds = (jobs + threads - 1) / threads; // whole rounds of jobs across the threads
			const auto time = static_cast<float>(rounds) * cost;
			if (time < bestTime)
			{
				bestLanes = lanes;
				bestTime = time;
			}
		}
		if (bestLanes < 4) segment.jobs = groupVoices(bestLanes / lanesPerVoice, segment.jobVoices, segment.jobVoiceCount);
	}
	if (threads > 1 && segment.jobs >= 2)
		workers->run(segment.jobs, &PluginProcessor::renderJobCallback, this);
	else
		for (int job = 0; job < segment.jobs; ++job) renderJob(job);
	for (int sample = 0; sample < samples; ++sample)
	{
		float left {}, right {};
		for (int unit = 0; unit < segment.units; ++unit)
		{
			float unitLeft {}, unitRight {};
			for (int index = 0; index < segment.unitVoiceCount[static_cast<std::size_t>(unit)]; ++index)
			{
				const auto voice = segment.unitVoices[static_cast<std::size_t>(unit)][static_cast<std::size_t>(index)];
				const auto* voiceSamples = voiceBuffer.data() + static_cast<std::size_t>(2 * voice) * voiceStride;
				unitLeft += voiceSamples[sample];
				unitRight += voiceSamples[voiceStride + static_cast<std::size_t>(sample)];
			}
			left += unitLeft;
			right += unitRight;
		}
		renderBlock.setSample(0, sample, left * outputGain);
		renderBlock.setSample(1, sample, right * outputGain);
	}
	if (oversampling.getActiveFactor() > 1)
	{
		auto outputSegment = outputBlock.getSubBlock(static_cast<std::size_t>(start), static_cast<std::size_t>(count));
		oversampling.processSamplesDown(outputSegment);
	}
}

juce::AudioProcessorEditor* PluginProcessor::createEditor() { return new PluginEditor(*this); }
int PluginProcessor::getNumPrograms() { return presetHost.numPrograms(); }
int PluginProcessor::getCurrentProgram() { return presetHost.currentProgram(); }
void PluginProcessor::setCurrentProgram(int index) { presetHost.selectProgram(index); }
const juce::String PluginProcessor::getProgramName(int index) { return presetHost.programName(index); }
juce::Result PluginProcessor::loadNextPreset() { return presetHost.loadAdjacentPreset(true); }
juce::Result PluginProcessor::loadPreviousPreset() { return presetHost.loadAdjacentPreset(false); }
juce::Result PluginProcessor::validatePresetSound(const presets::Preset& preset) const
{
	return preset.soundSchemaVersion != parameters::presetSoundSchemaVersion ? juce::Result::fail("Unsupported Mono preset sound schema")
		: presets::PresetSchema::validate(preset, parameters::presetProductIdentifier, parameterState, parameters::soundParameterIds);
}
juce::Result PluginProcessor::applyPreset(const presets::Preset& preset)
{
	if (const auto result = validatePresetSound(preset); result.failed()) return result;
	undoManager.beginNewTransaction("Load preset: " + preset.name);
	const auto result = presets::PresetSchema::apply(preset, parameters::presetProductIdentifier, parameterState, parameters::soundParameterIds, &undoManager);
	if (result.wasOk()) pendingPresetReset.store(true);
	return result;
}
bool PluginProcessor::matchesPresetSound(const presets::Preset& preset) const
{
	return validatePresetSound(preset).wasOk()
		&& presets::PresetSchema::matches(preset, parameters::presetProductIdentifier, parameterState, parameters::soundParameterIds);
}
void PluginProcessor::getStateInformation(juce::MemoryBlock& destination) { presetHost.save(destination); }
void PluginProcessor::setStateInformation(const void* data, int size) { juce::ignoreUnused(presetHost.restore(data, size)); }
}
