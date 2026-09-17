#include "PluginProcessor.h"

#include "FactoryPresets.h"

#include <vekt/presets/PresetSchema.h>

#include <juce_audio_utils/juce_audio_utils.h>

#include <span>

namespace vekt::saturator
{
namespace
{
bool migrateProjectState(juce::ValueTree& state, int sourceVersion)
{
	if (sourceVersion != 1)
		return false;

	if (!state.getChildWithName(state::StateManager::metadataType).isValid())
		state.addChild(juce::ValueTree(state::StateManager::metadataType), -1, nullptr);
	return true;
}
}

PluginProcessor::PluginProcessor()
	: AudioProcessor(BusesProperties()
		.withInput("Input", juce::AudioChannelSet::stereo(), true)
		.withOutput("Output", juce::AudioChannelSet::stereo(), true)),
	  parameterState(*this, &undoManager, parameters::stateType, parameters::createLayout()),
	  stateManager(parameterState, parameters::projectStateType,
		  parameters::projectStateVersion, migrateProjectState),
	  inputGainParameter(requireParameter(parameterState, parameters::inputGain)),
	  driveParameter(requireParameter(parameterState, parameters::drive)),
	  toneParameter(requireParameter(parameterState, parameters::tone)),
	  biasParameter(requireParameter(parameterState, parameters::bias)),
	  autoGainParameter(requireParameter(parameterState, parameters::autoGain)),
	  bypassParameter(requireParameter(parameterState, parameters::bypass)),
	  mixParameter(requireParameter(parameterState, parameters::mix)),
	  outputGainParameter(requireParameter(parameterState, parameters::outputGain)),
	  oversamplingFactorParameter(requireParameter(parameterState, parameters::oversamplingFactor)),
	  oversamplingPhaseParameter(requireParameter(parameterState, parameters::oversamplingPhase))
{
	const auto factoryPresetResult = addFactoryPresets(presetCatalog);
	jassert(factoryPresetResult.wasOk());
	juce::ignoreUnused(factoryPresetResult);
	if (presetCatalog.factoryPresetCount() > 0)
	{
		currentPresetIndex = 0;
		presets::Preset initialPreset;
		if (presetCatalog.loadFactoryPreset(0, initialPreset).wasOk())
			currentPresetSnapshot = std::move(initialPreset);
		stateManager.getMetadata().setProperty(
			parameters::currentFactoryPreset, presetCatalog.factoryPresetName(0), nullptr);
	}
	requestedOversamplingFactor.store(oversamplingFactorParameter->load());
	requestedOversamplingPhase.store(oversamplingPhaseParameter->load());
	parameterState.addParameterListener(parameters::oversamplingFactor, this);
	parameterState.addParameterListener(parameters::oversamplingPhase, this);
}

PluginProcessor::~PluginProcessor()
{
	stopTimer();
	cancelPendingUpdate();
	parameterState.removeParameterListener(parameters::oversamplingFactor, this);
	parameterState.removeParameterListener(parameters::oversamplingPhase, this);
}

void PluginProcessor::prepareToPlay(double sampleRate, int maximumBlockSize)
{
	const juce::dsp::ProcessSpec specification {
		sampleRate,
		static_cast<juce::uint32>(maximumBlockSize),
		2
	};

	oversampling.prepare(static_cast<std::size_t>(maximumBlockSize));
	requestedOversamplingFactor.store(oversamplingFactorParameter->load());
	requestedOversamplingPhase.store(oversamplingPhaseParameter->load());
	oversampling.activate(parameters::qualityFrom(
		requestedOversamplingFactor.load(), requestedOversamplingPhase.load()));
	toneStage.prepare(sampleRate, 2);
	tanhStage.prepare(sampleRate * static_cast<double>(oversampling.getActiveFactor()));
	autoGain.prepare(sampleRate);
	for (auto& dcBlocker : dcBlockers)
		dcBlocker.prepare(sampleRate);

	dryWetMixer.prepare(specification, oversampling.getMaximumLatencySamples());
	dryWetMixer.setWetLatency(oversampling.getActiveLatencySamples());
	bypassDelay.prepare(specification, oversampling.getMaximumLatencySamples());
	bypassDelay.setLatency(oversampling.getActiveLatencySamples());
	bypassScratch.setSize(2, maximumBlockSize, false, false, true);
	maximumPreparedBlockSize = maximumBlockSize;
	setLatencySamples(oversampling.getActiveLatencySamples());

	inputGain.prepare(specification);
	inputGain.setRampDurationSeconds(0.02);
	outputGain.prepare(specification);
	outputGain.setRampDurationSeconds(0.02);
	qualityChangePending.store(false);
	prepared.store(true);
}

void PluginProcessor::releaseResources()
{
	prepared.store(false);
	maximumPreparedBlockSize = 0;
}

bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
	return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
		&& layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
	observeTransport();
	processPreparedBlocks(buffer, midi, bypassParameter->load() >= 0.5f);
}

void PluginProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
	observeTransport();
	processPreparedBlocks(buffer, midi, true);
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
	toneStage.setSlopeDbPerOctave(toneParameter->load());
	tanhStage.setDriveLinear(juce::Decibels::decibelsToGain(driveParameter->load()));
	tanhStage.setBias(biasParameter->load());
	autoGain.setParameters(
		driveParameter->load(), biasParameter->load(), autoGainParameter->load() >= 0.5f);

	juce::dsp::AudioBlock<float> block(buffer);
	inputGain.process(juce::dsp::ProcessContextReplacing<float>(block));
	dryWetMixer.pushDrySamples(juce::dsp::AudioBlock<const float>(block));
	toneStage.processPre(block);

	auto oversampled = oversampling.processSamplesUp(juce::dsp::AudioBlock<const float>(block));
	for (std::size_t channel = 0; channel < oversampled.getNumChannels(); ++channel)
		tanhStage.process(std::span<float>(oversampled.getChannelPointer(channel), oversampled.getNumSamples()));
	oversampling.processSamplesDown(block);
	for (std::size_t channel = 0; channel < block.getNumChannels(); ++channel)
		dcBlockers[channel].process(std::span<float>(block.getChannelPointer(channel), block.getNumSamples()));
	toneStage.processPost(block);
	autoGain.process(block);

	dryWetMixer.mixWetSamples(block);
	outputGain.process(juce::dsp::ProcessContextReplacing<float>(block));
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
	return new juce::GenericAudioProcessorEditor(*this);
}

bool PluginProcessor::hasEditor() const { return true; }
const juce::String PluginProcessor::getName() const { return "Vekt Saturator"; }
bool PluginProcessor::acceptsMidi() const { return false; }
bool PluginProcessor::producesMidi() const { return false; }
bool PluginProcessor::isMidiEffect() const { return false; }
double PluginProcessor::getTailLengthSeconds() const { return 0.0; }
juce::AudioProcessorParameter* PluginProcessor::getBypassParameter() const
{
	return parameterState.getParameter(parameters::bypass);
}
int PluginProcessor::getNumPrograms()
{
	return static_cast<int>(presetCatalog.factoryPresetCount());
}

int PluginProcessor::getCurrentProgram() { return currentProgram; }

void PluginProcessor::setCurrentProgram(int index)
{
	if (index < 0)
		return;

	const auto factoryIndex = static_cast<std::size_t>(index);
	if (factoryIndex < presetCatalog.factoryPresetCount())
		juce::ignoreUnused(loadPreset(factoryIndex));
}

const juce::String PluginProcessor::getProgramName(int index)
{
	if (index < 0)
		return {};
	return presetCatalog.factoryPresetName(static_cast<std::size_t>(index));
}

void PluginProcessor::changeProgramName(int index, const juce::String& name) { juce::ignoreUnused(index, name); }

void PluginProcessor::getStateInformation(juce::MemoryBlock& destination)
{
	const auto state = stateManager.createState();
	if (const auto xml = state.createXml())
		copyXmlToBinary(*xml, destination);
}

void PluginProcessor::setStateInformation(const void* data, int size)
{
	const auto xml = getXmlFromBinary(data, size);
	if (xml == nullptr)
		return;

	auto state = juce::ValueTree::fromXml(*xml);
	if (stateManager.restoreState(state))
		restoreCurrentProgramFromMetadata();
}

presets::Preset PluginProcessor::createPreset(
	const juce::String& name, const juce::NamedValueSet& metadata) const
{
	return presets::PresetSchema::create(
		parameters::presetProductIdentifier,
		name,
		parameterState,
		parameters::soundParameterIds,
		metadata);
}

juce::Result PluginProcessor::applyPreset(const presets::Preset& preset)
{
	assertMessageThread();
	if (const auto result = presets::PresetSchema::validate(
			preset,
			parameters::presetProductIdentifier,
			parameterState,
			parameters::soundParameterIds);
		result.failed())
		return result;

	undoManager.beginNewTransaction("Load preset: " + preset.name);
	const auto result = presets::PresetSchema::apply(
		preset,
		parameters::presetProductIdentifier,
		parameterState,
		parameters::soundParameterIds,
		&undoManager);
	if (result.wasOk())
	{
		currentPresetIndex.reset();
		currentPresetSnapshot.reset();
	}
	return result;
}

juce::Result PluginProcessor::configureUserPresetDirectory(const juce::File& directory)
{
	assertMessageThread();
	if (directory == juce::File {})
		return juce::Result::fail("User preset directory is empty");

	std::optional<presets::PresetEntry> selectedEntry;
	if (currentPresetIndex && *currentPresetIndex < presetCatalog.entries().size())
		selectedEntry = presetCatalog.entries()[*currentPresetIndex];
	userPresetRepository = std::make_unique<presets::FilePresetRepository>(directory);
	presetCatalog.setUserRepository(userPresetRepository.get());
	currentPresetIndex = selectedEntry
		? presetCatalog.find(selectedEntry->name, selectedEntry->origin)
		: std::nullopt;
	if (currentPresetIndex)
	{
		presets::Preset preset;
		currentPresetSnapshot = presetCatalog.load(*currentPresetIndex, preset).wasOk()
			? std::optional<presets::Preset> { std::move(preset) }
			: std::nullopt;
	}
	else
		currentPresetSnapshot.reset();
	return juce::Result::ok();
}

juce::Result PluginProcessor::saveUserPreset(
	const juce::String& name, presets::PresetSaveMode mode)
{
	assertMessageThread();
	auto preset = createPreset(name);
	if (const auto result = presetCatalog.saveUserPreset(preset, mode); result.failed())
		return result;

	currentPresetIndex = presetCatalog.find(name.trim(), presets::PresetOrigin::user);
	currentPresetSnapshot = std::move(preset);
	return juce::Result::ok();
}

juce::Result PluginProcessor::removeUserPreset(const juce::String& name)
{
	assertMessageThread();
	std::optional<presets::PresetEntry> selectedEntry;
	if (currentPresetIndex && *currentPresetIndex < presetCatalog.entries().size())
		selectedEntry = presetCatalog.entries()[*currentPresetIndex];
	if (const auto result = presetCatalog.removeUserPreset(name); result.failed())
		return result;

	if (!selectedEntry
		|| (selectedEntry->origin == presets::PresetOrigin::user
			&& selectedEntry->name.equalsIgnoreCase(name)))
		currentPresetIndex.reset();
	else
		currentPresetIndex = presetCatalog.find(selectedEntry->name, selectedEntry->origin);
	if (!currentPresetIndex)
		currentPresetSnapshot.reset();
	return juce::Result::ok();
}

juce::Result PluginProcessor::loadPreset(std::size_t index)
{
	assertMessageThread();
	presets::Preset preset;
	if (const auto result = presetCatalog.load(index, preset); result.failed())
		return result;
	if (const auto result = applyPreset(preset); result.failed())
		return result;

	currentPresetIndex = index;
	currentPresetSnapshot = preset;
	if (presetCatalog.entries()[index].origin == presets::PresetOrigin::factory)
	{
		currentProgram = static_cast<int>(index);
		stateManager.getMetadata().setProperty(
			parameters::currentFactoryPreset, preset.name, nullptr);
	}
	return juce::Result::ok();
}

juce::Result PluginProcessor::loadNextPreset()
{
	if (presetCatalog.entries().empty())
		return juce::Result::fail("Preset catalog is empty");
	if (!currentPresetIndex)
		return loadPreset(0);

	const auto next = presetCatalog.nextIndex(*currentPresetIndex);
	return next ? loadPreset(*next) : juce::Result::fail("Current preset is unavailable");
}

juce::Result PluginProcessor::loadPreviousPreset()
{
	if (presetCatalog.entries().empty())
		return juce::Result::fail("Preset catalog is empty");
	if (!currentPresetIndex)
		return loadPreset(presetCatalog.entries().size() - 1);

	const auto previous = presetCatalog.previousIndex(*currentPresetIndex);
	return previous ? loadPreset(*previous) : juce::Result::fail("Current preset is unavailable");
}

const std::vector<presets::PresetEntry>& PluginProcessor::getPresetEntries() const noexcept
{
	return presetCatalog.entries();
}

std::optional<std::size_t> PluginProcessor::getCurrentPresetIndex() const noexcept
{
	return currentPresetIndex;
}

bool PluginProcessor::isCurrentPresetModified() const
{
	return currentPresetSnapshot
		&& !presets::PresetSchema::matches(
			*currentPresetSnapshot,
			parameters::presetProductIdentifier,
			parameterState,
			parameters::soundParameterIds);
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
	return stateManager.getMetadata();
}

void PluginProcessor::restoreCurrentProgramFromMetadata()
{
	const auto name = stateManager.getMetadata()
		.getProperty(parameters::currentFactoryPreset).toString();
	if (const auto index = presetCatalog.findFactoryPreset(name))
	{
		currentProgram = static_cast<int>(*index);
		currentPresetIndex = index;
		presets::Preset preset;
		currentPresetSnapshot = presetCatalog.loadFactoryPreset(*index, preset).wasOk()
			? std::optional<presets::Preset> { std::move(preset) }
			: std::nullopt;
	}
	else
	{
		currentProgram = 0;
		currentPresetIndex = presetCatalog.factoryPresetCount() > 0
			? std::optional<std::size_t> { 0 }
			: std::nullopt;
		currentPresetSnapshot.reset();
	}
}

dsp::OversamplingQuality PluginProcessor::getActiveQuality() const noexcept
{
	return oversampling.getActiveQuality();
}

bool PluginProcessor::hasPendingQualityChange() const noexcept
{
	return qualityChangePending.load();
}

std::atomic<float>* PluginProcessor::requireParameter(
	juce::AudioProcessorValueTreeState& state, const char* identifier)
{
	auto* parameter = state.getRawParameterValue(identifier);
	jassert(parameter != nullptr);
	return parameter;
}

void PluginProcessor::assertMessageThread()
{
	jassert(juce::MessageManager::getInstanceWithoutCreating() == nullptr
		|| juce::MessageManager::getInstanceWithoutCreating()->isThisTheMessageThread());
}

void PluginProcessor::parameterChanged(const juce::String& parameterId, float newValue)
{
	if (parameterId == parameters::oversamplingFactor)
		requestedOversamplingFactor.store(newValue);
	else if (parameterId == parameters::oversamplingPhase)
		requestedOversamplingPhase.store(newValue);
	else
		return;

	qualityChangePending.store(true);
	triggerAsyncUpdate();
}

void PluginProcessor::handleAsyncUpdate()
{
	applyPendingQualityChange();
}

void PluginProcessor::timerCallback()
{
	if (!qualityChangePending.load())
	{
		stopTimer();
		return;
	}

	const auto currentProcessCounter = processCounter.load();
	if (!transportPlaying.load())
	{
		stopTimer();
		applyPendingQualityChange();
		return;
	}
	if (currentProcessCounter == lastObservedProcessCounter)
	{
		transportPlaying.store(false);
		stopTimer();
		applyPendingQualityChange();
		return;
	}

	lastObservedProcessCounter = currentProcessCounter;
}

void PluginProcessor::observeTransport() noexcept
{
	auto isPlaying = false;
	if (const auto* playHead = getPlayHead())
		if (const auto position = playHead->getPosition())
			isPlaying = position->getIsPlaying();

	transportPlaying.store(isPlaying);
	processCounter.fetch_add(1);
}

void PluginProcessor::applyPendingQualityChange()
{
	if (!qualityChangePending.load() || !prepared.load())
		return;

	if (transportPlaying.load())
	{
		lastObservedProcessCounter = processCounter.load();
		startTimer(transportPollIntervalMs);
		return;
	}

	const auto quality = parameters::qualityFrom(
		requestedOversamplingFactor.load(), requestedOversamplingPhase.load());
	if (quality != oversampling.getActiveQuality())
	{
		suspendProcessing(true);
		oversampling.activate(quality);
		tanhStage.prepare(getSampleRate() * static_cast<double>(oversampling.getActiveFactor()));
		dryWetMixer.setWetLatency(oversampling.getActiveLatencySamples());
		dryWetMixer.reset();
		bypassDelay.setLatency(oversampling.getActiveLatencySamples());
		bypassDelay.reset();
		for (auto& dcBlocker : dcBlockers)
			dcBlocker.reset();
		setLatencySamples(oversampling.getActiveLatencySamples());
		suspendProcessing(false);
	}

	qualityChangePending.store(false);
}
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
	return new vekt::saturator::PluginProcessor();
}
