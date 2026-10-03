#include <vekt/kobber/PluginProcessor.h>

#include <vekt/kobber/FactoryPresets.h>
#include "LfoDestinations.h"
#include "KobberVoice.h"
#include "KobberParameterChoices.h"
#include "KobberRenderWorkers.h"
#include "KobberSettingsSnapshot.h"
#include "KobberVoiceAllocator.h"
#include "KobberRenderPlan.h"
#include <vekt/kobber/PluginEditor.h>

#include <vekt/plugin_support/RequireParameter.h>
#include <vekt/presets/PresetPaths.h>
#include <vekt/presets/PresetSchema.h>

#include <algorithm>
#include <array>
#include <limits>

namespace vekt::kobber
{
PluginProcessor::PluginProcessor()
	: AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
	  parameterState(*this, &undoManager, parameters::stateType, parameters::createLayout()),
	  presetHost(parameterState, { parameters::presetProductIdentifier, parameters::productName, parameters::presetSoundSchemaVersion }, {
		[this](const juce::String& name)
		{
			auto preset = presets::PresetSchema::create(parameters::presetProductIdentifier, name, parameterState, parameters::soundParameterIds);
			preset.soundSchemaVersion = parameters::presetSoundSchemaVersion;
			return preset;
		},
		[](presets::Preset& preset)
		{
			return preset.soundSchemaVersion == parameters::presetSoundSchemaVersion ? juce::Result::ok() : juce::Result::fail("Unsupported Kobber preset sound schema");
		},
		[this](const presets::Preset& preset) { return validatePresetSound(preset); },
		[this](const presets::Preset& preset) { return applyPreset(preset); },
		[this](const presets::Preset& preset) { return matchesPresetSound(preset); } }),
	  qualitySelection(parameterState, parameters::trackingOversampling, parameters::offlineOversampling),
	  cached(KobberParameterValues::resolve(parameterState))
{
	for (std::size_t index = 0; index < voices.size(); ++index)
		voices[index] = std::make_unique<KobberVoice>();
	for (auto& clock : lfoClocks) clock = std::make_unique<LfoClock>();
	vibratoClock = std::make_unique<LfoClock>();
	const auto factoryResult = addFactoryPresets(presetHost.catalog());
	jassert(factoryResult.wasOk());
	juce::ignoreUnused(factoryResult);
	juce::ignoreUnused(presetHost.configureUserPresetDirectory(presets::PresetPaths::desktop(parameters::productName)));
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
	if (identifier != parameters::multicore || !multicoreChoices.at(newValue)) return;
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
	const auto helpers = KobberRenderWorkers::defaultThreadCount();
	if (helpers <= 0) return;
	// The helpers join whatever workgroup the mailbox holds when they first wake.
	renderWorkerPool = std::make_unique<KobberRenderWorkers>(helpers, helperBlockSize.load(), helperSampleRate.load(), *workgroupMailbox);
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
	if (multicoreChoices.at(value(cached.multicore))) ensureRenderWorkers();
	for (auto& clock : lfoClocks) clock->reset();
	vibratoClock->reset();
	activeVoiceCount = voiceCounts.at(value(cached.voiceCount));
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

int PluginProcessor::getSoundingVoiceCount() const noexcept
{
	return static_cast<int>(std::count_if(voices.begin(), voices.end(), [](const auto& voice) { return voice->isActive(); }));
}

void PluginProcessor::applyConfigurationChanges()
{
	const auto voiceCount = voiceCounts.at(value(cached.voiceCount));
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
	const auto& jobVoices = segment.plan.jobs.voices[static_cast<std::size_t>(job)];
	const auto voiceCount = static_cast<std::size_t>(segment.plan.jobs.voiceCount[static_cast<std::size_t>(job)]);
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
			const auto beginVoice = [&](KobberVoice& voice)
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
	const KobberVoice* newest {};
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
	if (message.isNoteOn())
		allocator.noteOn(voices, allocationRules(), message.getChannel(), message.getNoteNumber(), message.getFloatVelocity(),
			voiceSettingsFrom(cached, transportBpm));
	else if (message.isNoteOff())
		allocator.noteOff(voices, allocationRules(), message.getChannel(), message.getNoteNumber(),
			[this] { return voiceSettingsFrom(cached, transportBpm); });
	else if (message.isAllSoundOff()) allocator.allNotesOff(voices, message.getChannel(), true);
	else if (message.isAllNotesOff()) allocator.allNotesOff(voices, message.getChannel(), false);
	else if (message.isResetAllControllers())
	{
		const auto channelIndex = static_cast<std::size_t>(message.getChannel() - 1);
		pitchBendByChannel[channelIndex] = 0.0f;
		modWheelByChannel[channelIndex] = pressureByChannel[channelIndex] = 0.0f;
		for (auto& voice : voices) if (voice->getChannel() == message.getChannel()) voice->setPolyPressure(0.0f);
		allocator.setSustain(voices, message.getChannel(), false);
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
		allocator.setSustain(voices, message.getChannel(), message.getControllerValue() >= 64);
	}
}

KobberVoiceAllocator<KobberVoice>::Rules PluginProcessor::allocationRules() const noexcept
{
	return { performanceModes.at(value(cached.performanceMode)), notePriorities.at(value(cached.notePriority)),
		value(cached.heldKeyReturn) >= 0.5f, activeVoiceCount };
}

void PluginProcessor::resetPlayingState()
{
	// Restart the shared modulation clocks with the voices, so output after a preset load does not depend on
	// how long the previous patch ran. A synced Free LFO then re-locks to the host position while it plays.
	for (auto& clock : lfoClocks) clock->reset();
	vibratoClock->reset();
	for (auto& voice : voices) voice->reset();
	allocator.reset();
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
	const auto settings = voiceSettingsFrom(cached, transportBpm);
	for (std::size_t index = 0; index < lfoClocks.size(); ++index)
	{
		lfoClocks[index]->setRate(settings.lfo[index].source.rateHz);
		lfoDisplayRates[index].store(std::clamp(settings.lfo[index].source.rateHz, minimumLfoRateHz, maximumLfoRateHz), std::memory_order_relaxed);
	}
	vibratoClock->setRate(value(cached.vibratoRate));
	const auto vibratoShape = vibratoShapes.at(value(cached.vibratoShape));
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
	// Notes only start at segment boundaries, so the render plan (KobberRenderPlan.h) is fixed for the segment. Its units
	// fix the summing order below; the batched solves share only tanh, which is per lane, so a voice renders the same
	// bits in any job.
	segment.settings = &settings;
	segment.channelControl = channelControl;
	segment.samples = samples;
	// Threads only pay off with enough samples to amortize waking the helpers.
	auto* workers = renderWorkers.load(std::memory_order_acquire);
	const auto threads = workers != nullptr && multicoreChoices.at(value(cached.multicore)) && samples >= 32 ? workers->threads() + 1 : 1;
	std::array<bool, 16> sounding {};
	for (std::size_t index = 0; index < voices.size(); ++index) sounding[index] = voices[index]->isActive();
	segment.plan = planRender(sounding, std::clamp(settings.unison, 1, 4), threads);
	if (threads > 1 && segment.plan.jobs.count >= 2)
		workers->run(segment.plan.jobs.count, &PluginProcessor::renderJobCallback, this);
	else
		for (int job = 0; job < segment.plan.jobs.count; ++job) renderJob(job);
	for (int sample = 0; sample < samples; ++sample)
	{
		float left {}, right {};
		for (int unit = 0; unit < segment.plan.units.count; ++unit)
		{
			float unitLeft {}, unitRight {};
			for (int index = 0; index < segment.plan.units.voiceCount[static_cast<std::size_t>(unit)]; ++index)
			{
				const auto voice = segment.plan.units.voices[static_cast<std::size_t>(unit)][static_cast<std::size_t>(index)];
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
	return preset.soundSchemaVersion != parameters::presetSoundSchemaVersion ? juce::Result::fail("Unsupported Kobber preset sound schema")
		: presets::PresetSchema::validate(preset, parameters::presetProductIdentifier, parameterState, parameters::soundParameterIds);
}
juce::Result PluginProcessor::applyPreset(const presets::Preset& preset)
{
	if (const auto result = validatePresetSound(preset); result.failed()) return result;
	auto* const undo = plugin_support::editorUndo(undoManager);
	if (undo != nullptr) undo->beginNewTransaction("Load preset: " + preset.name);
	const auto result = presets::PresetSchema::apply(preset, parameters::presetProductIdentifier, parameterState, parameters::soundParameterIds, undo);
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
