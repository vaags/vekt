#include "PluginProcessor.h"

#include "PluginEditor.h"

#include "FactoryPresets.h"
#include "UserPresetPaths.h"

#include <vekt/presets/PresetSchema.h>
#include <vekt/presets/PresetJsonCodec.h>

#include <juce_audio_utils/juce_audio_utils.h>

#include <span>

namespace vekt::rav
{
PluginProcessor::PluginProcessor()
	: AudioProcessor(BusesProperties()
						 .withInput("Input", juce::AudioChannelSet::stereo(), true)
						 .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
	  parameterState(*this, &undoManager, parameters::stateType, parameters::createLayout()),
	  stateManager(parameterState, parameters::projectStateType, 1),
	  presetSession(presetCatalog, { parameters::presetProductIdentifier, "Vekt Rav", 1 }, {
		[this](const juce::String& name) { return createPreset(name); }, {},
		[this](const presets::Preset& preset) { return presets::PresetSchema::validate(preset,
			parameters::presetProductIdentifier, parameterState, parameters::soundParameterIds); },
		[this](const presets::Preset& preset) { return applyPreset(preset); },
		[this](const presets::Preset& preset) { return presets::PresetSchema::matches(preset,
			parameters::presetProductIdentifier, parameterState, parameters::soundParameterIds); } }),
	  inputGainParameter(requireParameter(parameterState, parameters::inputGain)),
	  driveParameter(requireParameter(parameterState, parameters::drive)),
	  toneParameter(requireParameter(parameterState, parameters::tone)),
	  biasParameter(requireParameter(parameterState, parameters::bias)),
	  autoGainParameter(requireParameter(parameterState, parameters::autoGain)),
	  bypassParameter(requireParameter(parameterState, parameters::bypass)),
	  mixParameter(requireParameter(parameterState, parameters::mix)),
	  outputGainParameter(requireParameter(parameterState, parameters::outputGain)),
	  lowBandMixParameter(requireParameter(parameterState, parameters::lowBandMix)),
	  midBandMixParameter(requireParameter(parameterState, parameters::midBandMix)),
	  highBandMixParameter(requireParameter(parameterState, parameters::highBandMix)),
	  lowMidCutoffParameter(requireParameter(parameterState, parameters::lowMidCutoffHz)),
	  midHighCutoffParameter(requireParameter(parameterState, parameters::midHighCutoffHz)),
	  modeParameter(requireParameter(parameterState, parameters::mode)),
	  shapeParameter(requireParameter(parameterState, parameters::shape)),
	  dynamicsParameter(requireParameter(parameterState, parameters::dynamics)),
	  textureParameter(requireParameter(parameterState, parameters::texture)),
	  trackingOversamplingParameter(requireParameter(parameterState, parameters::trackingOversampling)),
	offlineOversamplingParameter(requireParameter(parameterState, parameters::offlineOversampling)),
	stageEnabledParameters { requireParameter(parameterState, parameters::stageEnabledSaturation),
									 requireParameter(parameterState, parameters::stageEnabledOverdrive),
									 requireParameter(parameterState, parameters::stageEnabledDistortion),
										 requireParameter(parameterState, parameters::stageEnabledFuzz) }
{
	presetSession.onSelectionChanged = [this]
	{
		const auto index = presetSession.currentIndex();
		if (index && presetSession.origin() == presets::PresetOrigin::factory)
		{
			currentProgram = static_cast<int>(*index);
			stateManager.getMetadata().setProperty(parameters::currentFactoryPreset,
				presetSession.loaded()->name, nullptr);
		}
	};
	const auto factoryPresetResult = addFactoryPresets(presetCatalog);
	jassert(factoryPresetResult.wasOk());
	juce::ignoreUnused(factoryPresetResult);
	if (presetCatalog.factoryPresetCount() > 0)
	{
		currentPresetIndex = 0;
		presets::Preset initialPreset;
		if (presetCatalog.loadFactoryPreset(0, initialPreset).wasOk())
		{
			currentPresetSnapshot = initialPreset;
			presetSession.adopt(initialPreset, presets::PresetOrigin::factory);
		}
		stateManager.getMetadata().setProperty(
			parameters::currentFactoryPreset, presetCatalog.factoryPresetName(0), nullptr);
	}
	requestedTrackingOversampling.store(trackingOversamplingParameter->load());
	requestedOfflineOversampling.store(offlineOversamplingParameter->load());
	parameterState.addParameterListener(parameters::trackingOversampling, this);
	parameterState.addParameterListener(parameters::offlineOversampling, this);
	if (wrapperType == wrapperType_VST3 || wrapperType == wrapperType_Standalone)
		juce::ignoreUnused(configureUserPresetDirectory(UserPresetPaths::desktop()));
	else if (wrapperType == wrapperType_AudioUnitv3)
	{
		juce::File directory;
		if (UserPresetPaths::auv3AppGroup(VEKT_AUV3_APP_GROUP_ID, directory).wasOk())
			juce::ignoreUnused(configureUserPresetDirectory(directory));
	}
}

PluginProcessor::~PluginProcessor()
{
	parameterState.removeParameterListener(parameters::trackingOversampling, this);
	parameterState.removeParameterListener(parameters::offlineOversampling, this);
}

void PluginProcessor::prepareToPlay(double sampleRate, int maximumBlockSize)
{
	preparedSampleRate = sampleRate;
	const juce::dsp::ProcessSpec specification {
		sampleRate,
		static_cast<juce::uint32>(maximumBlockSize),
		2
	};

	oversampling.prepare(static_cast<std::size_t>(maximumBlockSize));
	requestedTrackingOversampling.store(trackingOversamplingParameter->load());
	requestedOfflineOversampling.store(offlineOversamplingParameter->load());
	const auto initialQuality = isNonRealtime()
									? parameters::offlineQualityFrom(requestedOfflineOversampling.load())
									: parameters::trackingQualityFrom(requestedTrackingOversampling.load());
	oversampling.activate(initialQuality);
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
	for (auto& gain : bandAutoGain)
		gain.prepare(effectiveSampleRate);
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
		juce::jlimit(0, 3, juce::roundToInt(modeParameter->load())));
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
	if (!transportPlaying.load())
		applyPendingQualityChange();
	inputMeter.publish(buffer);
	processPreparedBlocks(buffer, midi, bypassParameter->load() >= 0.5f);
	outputMeter.publish(buffer);
}

void PluginProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
	observeTransport();
	if (!transportPlaying.load())
		applyPendingQualityChange();
	inputMeter.publish(buffer);
	processPreparedBlocks(buffer, midi, true);
	outputMeter.publish(buffer);
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
		juce::jlimit(0, 3, juce::roundToInt(modeParameter->load())));
	toneStage.setRampDurationSeconds(0.02);
	const auto usesDedicatedTone = currentMode == RavMode::fuzz;
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
				stage.setArtifactSafePolicy(currentMode == RavMode::fuzz);
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
		bandAutoGain[band].process(
			juce::dsp::AudioBlock<const float>(cleanBandBuffers[band]),
			juce::dsp::AudioBlock<float>(bandBuffers[band]),
			autoGainParameter->load() >= 0.5f);
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
			oversampled.setSample(channel, sample,
				bandBuffers[0].getSample(channel, sample)
				+ bandBuffers[1].getSample(channel, sample)
				+ bandBuffers[2].getSample(channel, sample));
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
const juce::String PluginProcessor::getName() const { return "Vekt Rav"; }
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
	stageChain.writeMetadata(stateManager.getMetadata());
	stateManager.getMetadata().setProperty("vektPresetSelection", presetSession.selectionState(), nullptr);
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
	{
		stageChain = RavStageChain::readMetadata(stateManager.getMetadata());
		restoreCurrentProgramFromMetadata();
		if (stateManager.getMetadata().hasProperty("vektPresetSelection"))
		{
			presetSession.clear();
			juce::ignoreUnused(presetSession.restoreSelection(
				stateManager.getMetadata().getProperty("vektPresetSelection").toString()));
		}
	}
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
		presetSession.clear();
	}
	return result;
}

juce::Result PluginProcessor::configureUserPresetDirectory(const juce::File& directory)
{
	assertMessageThread();
	if (directory == juce::File {})
		return juce::Result::fail("User preset directory is empty");

	std::optional<presets::PresetEntry> selectedEntry;
	currentPresetIndex = presetSession.currentIndex();
	if (currentPresetIndex && *currentPresetIndex < presetCatalog.entries().size())
		selectedEntry = presetCatalog.entries()[*currentPresetIndex];
	userPresetRepository = std::make_unique<presets::FilePresetRepository>(directory);
	presetCatalog.setUserRepository(userPresetRepository.get());
	currentPresetIndex = selectedEntry
		? presetCatalog.findById(selectedEntry->identifier, selectedEntry->origin)
		: std::nullopt;
	if (!currentPresetIndex) { currentPresetSnapshot.reset(); presetSession.clear(); }
	return juce::Result::ok();
}

juce::Result PluginProcessor::saveUserPreset(
	const juce::String& name, presets::PresetSaveMode mode)
{
	assertMessageThread();
	const auto tags = presetSession.loaded() ? presetSession.loaded()->tags : juce::StringArray {};
	if (const auto result = presetSession.save(name, {}, tags, mode); result.failed())
		return result;

	currentPresetIndex = presetCatalog.find(name.trim(), presets::PresetOrigin::user);
	currentPresetSnapshot = presetSession.loaded();
	return juce::Result::ok();
}

juce::Result PluginProcessor::importPreset(const juce::File& source)
{
	assertMessageThread();
	if (!source.existsAsFile())
		return juce::Result::fail("Preset file does not exist");
	presets::Preset preset;
	if (const auto result = presets::PresetJsonCodec::decode(source.loadFileAsString(), preset); result.failed())
		return result;
	return applyPreset(preset);
}

juce::Result PluginProcessor::exportPreset(const juce::File& destination, const juce::String& name) const
{
	assertMessageThread();
	juce::String json;
	auto preset = createPreset(name);
	if (presetSession.loaded())
	{
		preset.tags = presetSession.loaded()->tags;
		preset.metadata = presetSession.loaded()->metadata;
	}
	if (const auto result = presets::PresetJsonCodec::encode(preset, json); result.failed())
		return result;
	juce::TemporaryFile temporaryFile(destination);
	if (!temporaryFile.getFile().replaceWithText(json)
		|| !temporaryFile.overwriteTargetFileWithTemporary())
		return juce::Result::fail("Could not write preset");
	return juce::Result::ok();
}

juce::Result PluginProcessor::removeUserPreset(const juce::String& name)
{
	assertMessageThread();
	std::optional<presets::PresetEntry> selectedEntry;
	currentPresetIndex = presetSession.currentIndex();
	if (currentPresetIndex && *currentPresetIndex < presetCatalog.entries().size())
		selectedEntry = presetCatalog.entries()[*currentPresetIndex];
	if (const auto result = presetCatalog.removeUserPreset(name); result.failed())
		return result;

	if (!selectedEntry
		|| (selectedEntry->origin == presets::PresetOrigin::user
			&& selectedEntry->location.equalsIgnoreCase(name)))
		currentPresetIndex.reset();
	else
		currentPresetIndex = presetCatalog.findById(selectedEntry->identifier, selectedEntry->origin);
	if (!currentPresetIndex)
	{
		currentPresetSnapshot.reset();
		presetSession.clear();
	}
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
	presetSession.adopt(preset, presetCatalog.entries()[index].origin);
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
	currentPresetIndex = presetSession.currentIndex();
	if (presetCatalog.entries().empty())
		return juce::Result::fail("Preset catalog is empty");
	if (!currentPresetIndex)
		return loadPreset(0);

	const auto next = presetCatalog.nextIndex(*currentPresetIndex);
	return next ? loadPreset(*next) : juce::Result::fail("Current preset is unavailable");
}

juce::Result PluginProcessor::loadPreviousPreset()
{
	currentPresetIndex = presetSession.currentIndex();
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
	return presetSession.currentIndex();
}

bool PluginProcessor::isCurrentPresetModified() const
{
	return presetSession.modified();
}

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
	return stateManager.getMetadata();
}

RavStageChain::Order PluginProcessor::getStageOrder() const noexcept
{
	return stageChain.getOrder();
}

bool PluginProcessor::reorderStage(std::size_t index, int delta) noexcept
{
	if (stageChain.moveStage(index, delta))
	{
		stageChain.writeMetadata(stateManager.getMetadata());
		return true;
	}
	return false;
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
		if (currentPresetSnapshot) presetSession.adopt(*currentPresetSnapshot, presets::PresetOrigin::factory);
	}
	else
	{
		currentProgram = 0;
		currentPresetIndex = presetCatalog.factoryPresetCount() > 0
			? std::optional<std::size_t> { 0 }
			: std::nullopt;
		currentPresetSnapshot.reset();
		presetSession.clear();
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
	if (parameterId == parameters::trackingOversampling)
		requestedTrackingOversampling.store(newValue);
	else if (parameterId == parameters::offlineOversampling)
		requestedOfflineOversampling.store(newValue);
	else
		return;

	qualityChangePending.store(true);
}

void PluginProcessor::observeTransport() noexcept
{
	auto isPlaying = false;
	if (const auto* playHead = getPlayHead())
		if (const auto position = playHead->getPosition())
			isPlaying = position->getIsPlaying();

	transportPlaying.store(isPlaying);
}

void PluginProcessor::applyPendingQualityChange()
{
	if (!qualityChangePending.load() || !prepared.load())
		return;

	if (transportPlaying.load())
	{
		return;
	}

	const auto quality = isNonRealtime()
							 ? parameters::offlineQualityFrom(requestedOfflineOversampling.load())
							 : parameters::trackingQualityFrom(requestedTrackingOversampling.load());
	if (quality != oversampling.getActiveQuality())
	{
		oversampling.activate(quality);
		const auto effectiveSampleRate = preparedSampleRate
			* static_cast<double>(oversampling.getActiveFactor());
		for (auto& band : bandStages)
			for (auto& channel : band)
				for (auto& stage : channel)
						stage.prepare(effectiveSampleRate);
		for (auto& gain : bandAutoGain)
			gain.prepare(effectiveSampleRate);
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

	qualityChangePending.store(false);
}
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
	return new vekt::rav::PluginProcessor();
}
