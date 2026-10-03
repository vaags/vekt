#include <vekt/rav/PluginProcessor.h>

#include <vekt/rav/PluginEditor.h>

#include <vekt/rav/FactoryPresets.h>

#include <vekt/plugin_support/RequireParameter.h>
#include <vekt/presets/PresetPaths.h>
#include <vekt/presets/PresetSchema.h>

#include <juce_audio_utils/juce_audio_utils.h>

#include <span>

namespace vekt::rav
{
PluginProcessor::PluginProcessor()
	: AudioProcessor(BusesProperties()
						 .withInput("Input", juce::AudioChannelSet::stereo(), true)
						 .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
	  parameterState(*this, &undoManager, parameters::stateType, parameters::createLayout()),
	  presetHost(parameterState, { parameters::presetProductIdentifier, parameters::productName, parameters::presetSoundSchemaVersion }, {
		[this](const juce::String& name) { return createPreset(name); },
		[](presets::Preset& preset)
		{
			return preset.soundSchemaVersion == parameters::presetSoundSchemaVersion ? juce::Result::ok()
				: juce::Result::fail("Unsupported Rav preset sound schema");
		},
		[this](const presets::Preset& preset) { return validatePresetSound(preset); },
		[this](const presets::Preset& preset) { return applyPreset(preset); },
		[this](const presets::Preset& preset) { return matchesPresetSound(preset); } }),
	  qualitySelection(parameterState, parameters::trackingOversampling, parameters::offlineOversampling),
	  inputGainParameter(plugin_support::requireParameter(parameterState, parameters::inputGain)),
	  driveParameter(plugin_support::requireParameter(parameterState, parameters::drive)),
	  toneParameter(plugin_support::requireParameter(parameterState, parameters::tone)),
	  biasParameter(plugin_support::requireParameter(parameterState, parameters::bias)),
	  autoGainParameter(plugin_support::requireParameter(parameterState, parameters::autoGain)),
	  bypassParameter(plugin_support::requireParameter(parameterState, parameters::bypass)),
	  mixParameter(plugin_support::requireParameter(parameterState, parameters::mix)),
	  outputGainParameter(plugin_support::requireParameter(parameterState, parameters::outputGain)),
	  lowBandMixParameter(plugin_support::requireParameter(parameterState, parameters::lowBandMix)),
	  midBandMixParameter(plugin_support::requireParameter(parameterState, parameters::midBandMix)),
	  highBandMixParameter(plugin_support::requireParameter(parameterState, parameters::highBandMix)),
	  lowMidCutoffParameter(plugin_support::requireParameter(parameterState, parameters::lowMidCutoffHz)),
	  midHighCutoffParameter(plugin_support::requireParameter(parameterState, parameters::midHighCutoffHz)),
	  modeParameter(plugin_support::requireParameter(parameterState, parameters::mode)),
	  shapeParameter(plugin_support::requireParameter(parameterState, parameters::shape)),
	  dynamicsParameter(plugin_support::requireParameter(parameterState, parameters::dynamics)),
	  textureParameter(plugin_support::requireParameter(parameterState, parameters::texture)),
	stageEnabledParameters { plugin_support::requireParameter(parameterState, parameters::stageEnabledSaturation),
									 plugin_support::requireParameter(parameterState, parameters::stageEnabledOverdrive),
									 plugin_support::requireParameter(parameterState, parameters::stageEnabledDistortion),
										 plugin_support::requireParameter(parameterState, parameters::stageEnabledCircuitFuzz),
										 plugin_support::requireParameter(parameterState, parameters::stageEnabledGatedFuzz) }
{
	const auto factoryPresetResult = addFactoryPresets(presetHost.catalog());
	jassert(factoryPresetResult.wasOk());
	juce::ignoreUnused(factoryPresetResult);
	if (presetHost.catalog().factoryPresetCount() > 0)
	{
		presets::Preset initialPreset;
		if (presetHost.catalog().loadFactoryPreset(0, initialPreset).wasOk()
			&& presetHost.session().prepare(initialPreset).wasOk()
			&& presets::PresetSchema::apply(initialPreset,
				parameters::presetProductIdentifier, parameterState,
				parameters::soundParameterIds).wasOk())
		{
			RavStageChain::Order initialOrder {};
			const auto parsedOrder = RavStageChain::deserialise(
				initialPreset.soundState[RavStageChain::metadataPropertyName].toString(), initialOrder);
			jassert(parsedOrder);
			if (parsedOrder) juce::ignoreUnused(stageChain.setOrder(initialOrder));
			stageChain.writeMetadata(presetHost.metadata());
			presetHost.session().adopt(initialPreset, presets::PresetOrigin::factory);
		}
	}
	if (wrapperType == wrapperType_VST3 || wrapperType == wrapperType_Standalone
		|| wrapperType == wrapperType_AudioUnit)
		juce::ignoreUnused(configureUserPresetDirectory(presets::PresetPaths::desktop(parameters::productName)));
}

PluginProcessor::~PluginProcessor() = default;

void PluginProcessor::prepareToPlay(double sampleRate, int maximumBlockSize)
{
	preparedSampleRate = sampleRate;
	outputScope.prepare(sampleRate);
	const juce::dsp::ProcessSpec specification {
		sampleRate,
		static_cast<juce::uint32>(maximumBlockSize),
		2
	};

	oversampling.prepare(static_cast<std::size_t>(maximumBlockSize));
	oversampling.activate(qualitySelection.prepare(isNonRealtime()));
	toneStage.prepare(sampleRate, 2);
	const auto effectiveFactor = oversampling.getActiveFactor();
	const auto effectiveSampleRate = sampleRate * static_cast<double>(effectiveFactor);
	for (auto& band : bandStages)
		for (auto& channel : band)
			for (auto& stage : channel)
				stage.prepare(effectiveSampleRate);
	crossover.prepare(
		{ effectiveSampleRate, static_cast<juce::uint32>(maximumBlockSize * 4), 2 },
		{ lowMidCutoffParameter->load(), midHighCutoffParameter->load() });
	const auto maximumOversampledBlockSize = maximumBlockSize * static_cast<int>(oversampling.getMaximumFactor());
	for (auto& bands : bandBuffers)
		bands.setSize(2, maximumOversampledBlockSize, false, false, true);
	for (auto& bands : cleanBandBuffers)
		bands.setSize(2, maximumOversampledBlockSize, false, false, true);
	autoGain.prepare(effectiveSampleRate);
	const auto initialBandMixes = std::array {
		lowBandMixParameter->load() * 0.01f,
		midBandMixParameter->load() * 0.01f,
		highBandMixParameter->load() * 0.01f };
	for (std::size_t band = 0; band < bandMixSmoothers.size(); ++band)
	{
		bandMixSmoothers[band].prepare(effectiveSampleRate);
		bandMixSmoothers[band].setCurrentAndTargetValue(initialBandMixes[band]);
	}
	const auto initialMode = static_cast<RavMode>(
		juce::jlimit(0, static_cast<int>(ravModeCount - 1), juce::roundToInt(modeParameter->load())));
	const auto initialCompatibilityMode = stageEnabledParameters[0]->load() >= 0.5f
		&& std::all_of(stageEnabledParameters.begin() + 1, stageEnabledParameters.end(),
			[](const auto* parameter) { return parameter->load() < 0.5f; });
	for (auto& band : stageEnableSmoothers)
		for (std::size_t modeIndex = 0; modeIndex < band.size(); ++modeIndex)
		{
			const auto enabled = initialCompatibilityMode
				? modeIndex == static_cast<std::size_t>(initialMode)
				: stageEnabledParameters[modeIndex]->load() >= 0.5f;
			band[modeIndex].prepare(effectiveSampleRate, 0.01, 0.01, 0.01);
			band[modeIndex].setCurrentAndTargetValue(enabled ? 1.0f : 0.0f);
		}
	for (auto& dcBlocker : dcBlockers)
		dcBlocker.prepare(sampleRate);

	dryWetMixer.prepare(specification, oversampling.getMaximumLatencySamples());
	dryWetMixer.setRampLength(0.1);
	dryWetMixer.setWetLatency(oversampling.getActiveLatencySamples());
	bypassDelay.prepare(specification, oversampling.getMaximumLatencySamples());
	bypassDelay.setLatency(oversampling.getActiveLatencySamples());
	bypassScratch.setSize(2, maximumBlockSize, false, false, true);
	stageScratch.setSize(2, maximumOversampledBlockSize, false, false, true);
	maximumPreparedBlockSize = maximumBlockSize;
	setLatencySamples(oversampling.getActiveLatencySamples());

	inputGain.prepare(specification);
	inputGain.setRampDurationSeconds(0.02);
	outputGain.prepare(specification);
	outputGain.setRampDurationSeconds(0.02);
}

void PluginProcessor::releaseResources()
{
	qualitySelection.release();
	maximumPreparedBlockSize = 0;
}

bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
	return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
		&& layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
	applyPendingQualityChange();
	inputMeter.publish(buffer);
	processPreparedBlocks(buffer, midi, bypassParameter->load() >= 0.5f);
	outputMeter.publish(buffer);
	outputScope.publish(buffer);
}

void PluginProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
	applyPendingQualityChange();
	inputMeter.publish(buffer);
	processPreparedBlocks(buffer, midi, true);
	outputMeter.publish(buffer);
	outputScope.publish(buffer);
}

void PluginProcessor::processPreparedBlocks(
	juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, bool bypassed)
{
	jassert(maximumPreparedBlockSize > 0);
	if (maximumPreparedBlockSize <= 0)
	{
		buffer.clear();
		return;
	}

	for (auto offset = 0; offset < buffer.getNumSamples(); offset += maximumPreparedBlockSize)
	{
		const auto blockSize = std::min(maximumPreparedBlockSize, buffer.getNumSamples() - offset);
		juce::AudioBuffer<float> block(
			buffer.getArrayOfWritePointers(), buffer.getNumChannels(), offset, blockSize);

		if (bypassed)
			processBypassedBlock(block, midi);
		else
		{
			bypassDelay.advance(juce::dsp::AudioBlock<const float>(block));
			processEffectBlock(block, midi);
		}
	}
}

void PluginProcessor::processBypassedBlock(
	juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
	for (auto channel = 0; channel < buffer.getNumChannels(); ++channel)
		bypassScratch.copyFrom(channel, 0, buffer, channel, 0, buffer.getNumSamples());

	auto* scratchChannels = bypassScratch.getArrayOfWritePointers();
	juce::AudioBuffer<float> scratch(scratchChannels, buffer.getNumChannels(), buffer.getNumSamples());
	processEffectBlock(scratch, midi);
	bypassDelay.processReplacing(juce::dsp::AudioBlock<float>(buffer));
}

void PluginProcessor::processEffectBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
	juce::ignoreUnused(midi);
	juce::ScopedNoDenormals noDenormals;

	for (auto channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
		buffer.clear(channel, 0, buffer.getNumSamples());

	inputGain.setGainDecibels(inputGainParameter->load());
	outputGain.setGainDecibels(outputGainParameter->load());
	dryWetMixer.setWetProportion(mixParameter->load() * 0.01f);
	const auto currentMode = static_cast<RavMode>(
		juce::jlimit(0, static_cast<int>(ravModeCount - 1), juce::roundToInt(modeParameter->load())));
	toneStage.setRampDurationSeconds(0.02);
	const auto usesDedicatedTone = currentMode == RavMode::circuitFuzz
		|| currentMode == RavMode::gatedFuzz;
	toneStage.setSlopeDbPerOctave(usesDedicatedTone ? 0.0f
		: -parameters::toneSlopeFromUserValue(toneParameter->load()));

	juce::dsp::AudioBlock<float> block(buffer);
	inputGain.process(juce::dsp::ProcessContextReplacing<float>(block));
	dryWetMixer.pushDrySamples(juce::dsp::AudioBlock<const float>(block));
	toneStage.processPre(block);

	auto oversampled = oversampling.processSamplesUp(juce::dsp::AudioBlock<const float>(block));
	crossover.requestCutoffs({ lowMidCutoffParameter->load(), midHighCutoffParameter->load() });
	const auto bandCount = bandBuffers.size();
	for (std::size_t band = 0; band < bandCount; ++band)
	{
		bandBuffers[band].clear();
		cleanBandBuffers[band].clear();
	}
	const auto sampleCount = oversampled.getNumSamples();
	const auto samples = static_cast<int>(sampleCount);
	crossover.process(
		juce::dsp::AudioBlock<const float>(oversampled),
		{ juce::dsp::AudioBlock<float>(bandBuffers[0].getArrayOfWritePointers(), 2, 0, sampleCount),
			juce::dsp::AudioBlock<float>(bandBuffers[1].getArrayOfWritePointers(), 2, 0, sampleCount),
			juce::dsp::AudioBlock<float>(bandBuffers[2].getArrayOfWritePointers(), 2, 0, sampleCount) });

	const auto bandMixes = std::array {
		lowBandMixParameter->load() * 0.01f,
		midBandMixParameter->load() * 0.01f,
		highBandMixParameter->load() * 0.01f };
	for (std::size_t band = 0; band < bandMixSmoothers.size(); ++band)
		bandMixSmoothers[band].setTargetValue(bandMixes[band]);
	const auto compatibilityMode = stageEnabledParameters[0]->load() >= 0.5f
		&& std::all_of(stageEnabledParameters.begin() + 1, stageEnabledParameters.end(),
			[](const auto* parameter) { return parameter->load() < 0.5f; });
	for (auto& band : stageEnableSmoothers)
		for (std::size_t modeIndex = 0; modeIndex < band.size(); ++modeIndex)
		{
			const auto enabled = compatibilityMode
				? modeIndex == static_cast<std::size_t>(currentMode)
				: stageEnabledParameters[modeIndex]->load() >= 0.5f;
			band[modeIndex].setTargetValue(enabled ? 1.0f : 0.0f);
		}
	for (std::size_t band = 0; band < bandCount; ++band)
	{
		for (auto channel = 0; channel < 2; ++channel)
			cleanBandBuffers[band].copyFrom(channel, 0, bandBuffers[band], channel, 0, samples);
		for (const auto mode : stageChain.getOrder())
		{
			const auto modeIndex = static_cast<std::size_t>(mode);
			auto& enableSmoother = stageEnableSmoothers[band][modeIndex];
			if (enableSmoother.getCurrentValue() <= 0.0f && enableSmoother.getTargetValue() <= 0.0f)
				continue;

			for (auto channel = 0; channel < 2; ++channel)
			{
				auto& stage = bandStages[band][static_cast<std::size_t>(channel)][modeIndex];
				stage.setArtifactSafePolicy(mode == RavMode::circuitFuzz
					|| mode == RavMode::gatedFuzz);
				stage.setParameters(mode, driveParameter->load(), biasParameter->load(),
					shapeParameter->load(), dynamicsParameter->load(),
					textureParameter->load(), toneParameter->load());
				stageScratch.copyFrom(channel, 0, bandBuffers[band], channel, 0, samples);
				stage.process(std::span<float>(stageScratch.getWritePointer(channel),
					static_cast<std::size_t>(samples)));
			}
			for (auto sample = 0; sample < samples; ++sample)
			{
				const auto amount = enableSmoother.getNextValue();
				for (auto channel = 0; channel < 2; ++channel)
				{
					const auto dry = bandBuffers[band].getSample(channel, sample);
					const auto wet = stageScratch.getSample(channel, sample);
					bandBuffers[band].setSample(channel, sample, dry + (wet - dry) * amount);
				}
			}
		}
		for (auto channel = 0; channel < 2; ++channel)
			for (auto sample = 0; sample < samples; ++sample)
			{
				const auto bandMix = channel == 0 ? bandMixSmoothers[band].getNextValue()
					: bandMixSmoothers[band].getCurrentValue();
				bandBuffers[band].setSample(channel, sample,
					cleanBandBuffers[band].getSample(channel, sample) * (1.0f - bandMix)
					+ bandBuffers[band].getSample(channel, sample) * bandMix);
			}
	}
	for (auto channel = 0; channel < 2; ++channel)
		for (auto sample = 0; sample < samples; ++sample)
		{
			cleanBandBuffers[0].setSample(channel, sample,
				cleanBandBuffers[0].getSample(channel, sample)
				+ cleanBandBuffers[1].getSample(channel, sample)
				+ cleanBandBuffers[2].getSample(channel, sample));
			oversampled.setSample(channel, sample,
				bandBuffers[0].getSample(channel, sample)
				+ bandBuffers[1].getSample(channel, sample)
				+ bandBuffers[2].getSample(channel, sample));
		}
	autoGain.process(
		juce::dsp::AudioBlock<const float>(cleanBandBuffers[0].getArrayOfReadPointers(), 2, 0, sampleCount),
		juce::dsp::AudioBlock<float>(oversampled),
		autoGainParameter->load() >= 0.5f);
	oversampling.processSamplesDown(block);
	for (std::size_t channel = 0; channel < block.getNumChannels(); ++channel)
		dcBlockers[channel].process(std::span<float>(block.getChannelPointer(channel), block.getNumSamples()));
	toneStage.processPost(block);
	dryWetMixer.mixWetSamples(block);
	outputGain.process(juce::dsp::ProcessContextReplacing<float>(block));
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
	return new PluginEditor(*this);
}

bool PluginProcessor::hasEditor() const { return true; }
const juce::String PluginProcessor::getName() const { return parameters::productName; }
bool PluginProcessor::acceptsMidi() const { return false; }
bool PluginProcessor::producesMidi() const { return false; }
bool PluginProcessor::isMidiEffect() const { return false; }
double PluginProcessor::getTailLengthSeconds() const { return 0.0; }
juce::AudioProcessorParameter* PluginProcessor::getBypassParameter() const
{
	return parameterState.getParameter(parameters::bypass);
}
int PluginProcessor::getNumPrograms() { return presetHost.numPrograms(); }
int PluginProcessor::getCurrentProgram() { return presetHost.currentProgram(); }
void PluginProcessor::setCurrentProgram(int index) { presetHost.selectProgram(index); }
const juce::String PluginProcessor::getProgramName(int index) { return presetHost.programName(index); }
void PluginProcessor::changeProgramName(int index, const juce::String& name) { juce::ignoreUnused(index, name); }

void PluginProcessor::getStateInformation(juce::MemoryBlock& destination)
{
	stageChain.writeMetadata(presetHost.metadata());
	presetHost.save(destination);
}

void PluginProcessor::setStateInformation(const void* data, int size)
{
	if (presetHost.restore(data, size))
		stageChain = RavStageChain::readMetadata(presetHost.metadata());
}

presets::Preset PluginProcessor::createPreset(
	const juce::String& name, const juce::NamedValueSet& metadata) const
{
	auto preset = presets::PresetSchema::create(
		parameters::presetProductIdentifier,
		name,
		parameterState,
		parameters::soundParameterIds,
		metadata);
	preset.soundSchemaVersion = parameters::presetSoundSchemaVersion;
	preset.soundState.set(RavStageChain::metadataPropertyName, RavStageChain::serialise(stageChain.getOrder()));
	return preset;
}

juce::Result PluginProcessor::applyPreset(const presets::Preset& preset)
{
	assertMessageThread();
	if (preset.soundSchemaVersion != parameters::presetSoundSchemaVersion)
		return juce::Result::fail("Unsupported Rav preset sound schema");
	if (const auto result = validatePresetSound(preset); result.failed())
		return result;
	RavStageChain::Order order {};
	if (!RavStageChain::deserialise(preset.soundState[RavStageChain::metadataPropertyName].toString(), order))
		return juce::Result::fail("Rav preset stage order is invalid");

	juce::ignoreUnused(parameterState.copyState());
	undoManager.beginNewTransaction("Load preset: " + preset.name);
	const auto result = presets::PresetSchema::apply(
		preset,
		parameters::presetProductIdentifier,
		parameterState,
		parameters::soundParameterIds,
		&undoManager);
	if (result.wasOk())
	{
		juce::ignoreUnused(stageChain.setOrder(order));
		stageChain.writeMetadata(presetHost.metadata());
		presetHost.session().clear();
	}
	return result;
}

juce::Result PluginProcessor::validatePresetSound(const presets::Preset& preset) const
{
	if (const auto result = presets::PresetSchema::validate(preset,
		parameters::presetProductIdentifier, parameterState, parameters::soundParameterIds); result.failed())
		return result;
	RavStageChain::Order order {};
	return RavStageChain::deserialise(preset.soundState[RavStageChain::metadataPropertyName].toString(), order)
		? juce::Result::ok() : juce::Result::fail("Rav preset stage order is invalid");
}

bool PluginProcessor::matchesPresetSound(const presets::Preset& preset) const
{
	if (validatePresetSound(preset).failed()
		|| !presets::PresetSchema::matches(preset, parameters::presetProductIdentifier,
			parameterState, parameters::soundParameterIds))
		return false;
	return RavStageChain::serialise(stageChain.getOrder())
		== preset.soundState[RavStageChain::metadataPropertyName].toString();
}

juce::Result PluginProcessor::configureUserPresetDirectory(const juce::File& directory)
{
	return presetHost.configureUserPresetDirectory(directory);
}

juce::Result PluginProcessor::loadNextPreset() { return presetHost.loadAdjacentPreset(true); }
juce::Result PluginProcessor::loadPreviousPreset() { return presetHost.loadAdjacentPreset(false); }

std::array<float, 2> PluginProcessor::consumeInputPeaks() noexcept
{
	return inputMeter.consumePeaks();
}

std::array<float, 2> PluginProcessor::consumeOutputPeaks() noexcept
{
	return outputMeter.consumePeaks();
}

juce::AudioProcessorValueTreeState& PluginProcessor::getParameters() noexcept
{
	return parameterState;
}

juce::UndoManager& PluginProcessor::getUndoManager() noexcept
{
	return undoManager;
}

juce::ValueTree& PluginProcessor::getProjectMetadata() noexcept
{
	return presetHost.metadata();
}

RavStageChain::Order PluginProcessor::getStageOrder() const noexcept
{
	return stageChain.getOrder();
}

bool PluginProcessor::reorderStage(std::size_t index, int delta) noexcept
{
	if (stageChain.moveStage(index, delta))
	{
		stageChain.writeMetadata(presetHost.metadata());
		return true;
	}
	return false;
}

dsp::OversamplingQuality PluginProcessor::getActiveQuality() const noexcept { return qualitySelection.active(); }
bool PluginProcessor::hasPendingQualityChange() const noexcept { return qualitySelection.pending(); }

void PluginProcessor::assertMessageThread()
{
	jassert(juce::MessageManager::getInstanceWithoutCreating() == nullptr
		|| juce::MessageManager::getInstanceWithoutCreating()->isThisTheMessageThread());
}

void PluginProcessor::applyPendingQualityChange()
{
	// Applies at once, during playback too; the audio may drop at the switch (ADR 0010).
	const auto quality = qualitySelection.takeRequest(isNonRealtime());
	if (!quality)
		return;

	oversampling.activate(*quality);
	const auto effectiveSampleRate = preparedSampleRate
		* static_cast<double>(oversampling.getActiveFactor());
	for (auto& band : bandStages)
		for (auto& channel : band)
			for (auto& stage : channel)
				stage.prepare(effectiveSampleRate);
	autoGain.prepare(effectiveSampleRate);
	for (auto& bandMix : bandMixSmoothers)
		bandMix.prepare(effectiveSampleRate);
	crossover.prepare(
		{ effectiveSampleRate, static_cast<juce::uint32>(maximumPreparedBlockSize * 4), 2 },
		{ lowMidCutoffParameter->load(), midHighCutoffParameter->load() });
	dryWetMixer.setWetLatency(oversampling.getActiveLatencySamples());
	dryWetMixer.reset();
	bypassDelay.setLatency(oversampling.getActiveLatencySamples());
	bypassDelay.reset();
	for (auto& dcBlocker : dcBlockers)
		dcBlocker.reset();
	setLatencySamples(oversampling.getActiveLatencySamples());
}
}
