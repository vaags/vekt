#include "PluginProcessor.h"

#include "PluginEditor.h"

#include <vekt/presets/PresetPaths.h>
#include <vekt/presets/PresetSchema.h>

#include <cmath>
#include <numbers>

namespace vekt::glimmer
{
namespace
{
float gainFromDb(float decibels) noexcept
{
	return std::pow(10.0f, decibels / 20.0f);
}

RotarySpeedMode speedModeFrom(float value) noexcept
{
	switch (juce::jlimit(0, 2, juce::roundToInt(value)))
	{
	case 1: return RotarySpeedMode::fast;
	case 2: return RotarySpeedMode::autoMode;
	default: return RotarySpeedMode::slow;
	}
}
}

PluginProcessor::PluginProcessor()
	: AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
		.withOutput("Output", juce::AudioChannelSet::stereo(), true)),
	  parameterState(*this, &undoManager, parameters::stateType, parameters::createLayout()),
	  stateManager(parameterState, parameters::projectStateType, 1),
	  presetSession(presetCatalog, { parameters::presetProductIdentifier, "Vekt Glimmer", 1 }, {
		[this](const juce::String& name) { return createPreset(name); },
		[](presets::Preset& preset)
		{
			return preset.soundSchemaVersion == 1 ? juce::Result::ok()
				: juce::Result::fail("Unsupported Glimmer preset sound schema");
		},
		[this](const presets::Preset& preset) { return validatePresetSound(preset); },
		[this](const presets::Preset& preset) { return applyPreset(preset); },
		[this](const presets::Preset& preset) { return matchesPresetSound(preset); } }),
	  inputGainParameter(requireParameter(parameterState, parameters::inputGain)),
	  preampDriveParameter(requireParameter(parameterState, parameters::preampDrive)),
	  balanceParameter(requireParameter(parameterState, parameters::hornDrumBalance)),
	  micAngleParameter(requireParameter(parameterState, parameters::micAngle)),
	  micDistanceParameter(requireParameter(parameterState, parameters::micDistance)),
	  slowSpeedParameter(requireParameter(parameterState, parameters::slowSpeed)),
	  fastSpeedParameter(requireParameter(parameterState, parameters::fastSpeed)),
	  accelerationParameter(requireParameter(parameterState, parameters::accelerationTime)),
	  decelerationParameter(requireParameter(parameterState, parameters::decelerationTime)),
	  hornToneParameter(requireParameter(parameterState, parameters::hornTone)),
	  drumToneParameter(requireParameter(parameterState, parameters::drumTone)),
	  speedModeParameter(requireParameter(parameterState, parameters::speedMode)),
	  sensitivityParameter(requireParameter(parameterState, parameters::sensitivity)),
	  autoGainParameter(requireParameter(parameterState, parameters::autoGain)),
	  bypassParameter(requireParameter(parameterState, parameters::bypass)),
	  mixParameter(requireParameter(parameterState, parameters::mix)),
	  outputGainParameter(requireParameter(parameterState, parameters::outputGain)),
	  trackingOversamplingParameter(requireParameter(parameterState, parameters::trackingOversampling)),
	  offlineOversamplingParameter(requireParameter(parameterState, parameters::offlineOversampling)),
	  modelParameter(requireParameter(parameterState, parameters::cabinetModel)),
	  brakeParameter(requireParameter(parameterState, parameters::brake)),
	  widthParameter(requireParameter(parameterState, parameters::stereoWidth)),
	  manualParameter(requireParameter(parameterState, parameters::manualSpeedEnabled)),
	  positionParameter(requireParameter(parameterState, parameters::speedPosition))
{
	requestedTrackingOversampling.store(trackingOversamplingParameter->load());
	requestedOfflineOversampling.store(offlineOversamplingParameter->load());
	parameterState.addParameterListener(parameters::trackingOversampling, this);
	parameterState.addParameterListener(parameters::offlineOversampling, this);
	juce::ignoreUnused(configureUserPresetDirectory(presets::PresetPaths::desktop("Vekt Glimmer")));
}

PluginProcessor::~PluginProcessor()
{
	parameterState.removeParameterListener(parameters::trackingOversampling, this);
	parameterState.removeParameterListener(parameters::offlineOversampling, this);
}

void PluginProcessor::prepareToPlay(double newSampleRate, int newMaximumBlockSize)
{
	sampleRateHz = newSampleRate;
	maximumBlockSize = std::max(newMaximumBlockSize, 1);
	const juce::dsp::ProcessSpec spec { sampleRateHz, static_cast<juce::uint32>(maximumBlockSize), 2 };
	oversampling.prepare(static_cast<std::size_t>(maximumBlockSize));
	requestedTrackingOversampling.store(trackingOversamplingParameter->load());
	requestedOfflineOversampling.store(offlineOversamplingParameter->load());
	oversampling.activate(isNonRealtime()
		? parameters::offlineQualityFrom(requestedOfflineOversampling.load())
		: parameters::trackingQualityFrom(requestedTrackingOversampling.load()));
	micLatencySamples = RotaryEngine::latencySamples(sampleRateHz);
	bypassDelay.prepare(spec, oversampling.getMaximumLatencySamples() + micLatencySamples);
	dryWetMixer.prepare(spec, oversampling.getMaximumLatencySamples() + micLatencySamples);
	dryWetMixer.setRampLength(0.1);
	updateLatency();
	for (auto& engine : engines) engine.prepare(sampleRateHz, maximumBlockSize);
	activeEngine = 0;
	switchingModel = false;
	modelWarmup = modelFade = 0;
	const auto selectedModel = juce::jlimit(0, 2, juce::roundToInt(modelParameter->load()));
	for (auto& engine : engines) engine.start(static_cast<CabinetModel>(selectedModel), rotarySettings());
	activeModel.store(selectedModel);
	modelPending.store(false);
	inputTransition.prepare(sampleRateHz);
	outputTransition.prepare(sampleRateHz);
	widthTransition.prepare(sampleRateHz, 0.05);
	bypassTransition.prepare(sampleRateHz, 0.01);
	hasProcessed = false;
	driveTransition.prepare(sampleRateHz * static_cast<double>(oversampling.getActiveFactor()));
	inputTransition.setCurrentAndTargetValue(inputGainParameter->load());
	outputTransition.setCurrentAndTargetValue(outputGainParameter->load());
	widthTransition.setCurrentAndTargetValue(widthParameter->load() * 0.01f);
	driveTransition.setCurrentAndTargetValue(preampDriveParameter->load());
	preampLow = preampHigh = {};
	for (auto& blocker : dcBlockers)
		blocker.prepare(sampleRateHz);
	autoGain.prepare(sampleRateHz);
	autoDetector.prepare(sampleRateHz);
	referenceBuffer.setSize(2, maximumBlockSize, false, false, true);
	bypassBuffer.setSize(2, maximumBlockSize, false, false, true);
	qualityChangePending.store(false);
	prepared.store(true);
}

void PluginProcessor::updateLatency()
{
	const auto latency = oversampling.getActiveLatencySamples() + micLatencySamples;
	bypassDelay.setLatency(latency);
	dryWetMixer.setWetLatency(latency);
	setLatencySamples(latency);
}

RotarySettings PluginProcessor::rotarySettings() const noexcept
{
	return { balanceParameter->load(), micAngleParameter->load(), micDistanceParameter->load(),
		hornToneParameter->load(), drumToneParameter->load(), slowSpeedParameter->load(), fastSpeedParameter->load(),
		accelerationParameter->load(), decelerationParameter->load(), speedModeFrom(speedModeParameter->load()),
		brakeParameter->load() >= 0.5f, manualParameter->load() >= 0.5f, positionParameter->load() * 0.01f };
}

void PluginProcessor::releaseResources()
{
	prepared.store(false);
	maximumBlockSize = 0;
	micLatencySamples = 0;
	referenceBuffer.setSize(0, 0);
	bypassBuffer.setSize(0, 0);
}

bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
	return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
		&& layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
	observeTransport();
	if (!transportPlaying.load()) applyPendingQualityChange();
	inputMeter.publish(buffer);
	process(buffer, bypassParameter->load() >= 0.5f);
	outputMeter.publish(buffer);
}

void PluginProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
	observeTransport();
	if (!transportPlaying.load()) applyPendingQualityChange();
	inputMeter.publish(buffer);
	process(buffer, true);
	outputMeter.publish(buffer);
}

void PluginProcessor::process(juce::AudioBuffer<float>& buffer, bool bypassed)
{
	if (maximumBlockSize <= 0)
	{
		buffer.clear();
		return;
	}

	juce::ScopedNoDenormals noDenormals;
	if (!hasProcessed && buffer.getNumSamples() > 0)
	{
		bypassTransition.setCurrentAndTargetValue(bypassed ? 1.0f : 0.0f);
		hasProcessed = true;
	}
	bypassTransition.setTargetValue(bypassed ? 1.0f : 0.0f);
	for (int offset = 0; offset < buffer.getNumSamples(); offset += maximumBlockSize)
	{
		const auto samples = std::min(maximumBlockSize, buffer.getNumSamples() - offset);
		juce::AudioBuffer<float> block(buffer.getArrayOfWritePointers(), 2, offset, samples);
		for (int channel = 0; channel < 2; ++channel)
			bypassBuffer.copyFrom(channel, 0, block, channel, 0, samples);
		auto rawBlock = juce::dsp::AudioBlock<float>(bypassBuffer).getSubBlock(0, static_cast<std::size_t>(samples));
		bypassDelay.processReplacing(rawBlock);

		inputTransition.setTargetValue(inputGainParameter->load());
		outputTransition.setTargetValue(outputGainParameter->load());
		driveTransition.setTargetValue(preampDriveParameter->load());
		widthTransition.setTargetValue(widthParameter->load() * 0.01f);
		dryWetMixer.setWetProportion(mixParameter->load() * 0.01f);
		const auto settings = rotarySettings();
		const auto requestedModel = static_cast<CabinetModel>(juce::jlimit(0, 2, juce::roundToInt(modelParameter->load())));
		if (!switchingModel && requestedModel != engines[activeEngine].getModel())
		{
			engines[1 - activeEngine].start(requestedModel, settings, &engines[activeEngine]);
			modelWarmup = static_cast<int>(std::ceil(sampleRateHz * 0.1));
			modelFade = 0;
			switchingModel = true;
		}
		engines[activeEngine].setSettings(settings);
		if (switchingModel) engines[1 - activeEngine].setSettings(settings);
		modelPending.store(switchingModel || requestedModel != engines[activeEngine].getModel());
		autoDetector.setSensitivity(sensitivityParameter->load() * 0.01f);
		for (int sample = 0; sample < samples; ++sample)
		{
			const auto inputGain = gainFromDb(inputTransition.getNextValue());
			for (int channel = 0; channel < 2; ++channel)
			{
				const auto input = block.getSample(channel, sample) * inputGain;
				referenceBuffer.setSample(channel, sample, input);
				block.setSample(channel, sample, input);
			}
		}
		dryWetMixer.pushDrySamples(juce::dsp::AudioBlock<const float>(block));
		auto oversampled = oversampling.processSamplesUp(juce::dsp::AudioBlock<const float>(block));
		const auto preampRate = static_cast<float>(sampleRateHz * static_cast<double>(oversampling.getActiveFactor()));
		const auto lowCoefficient = 1.0f - std::exp(-2.0f * std::numbers::pi_v<float> * 1200.0f / preampRate);
		const auto highCoefficient = 1.0f - std::exp(-2.0f * std::numbers::pi_v<float> * 15000.0f / preampRate);
		for (std::size_t sample = 0; sample < oversampled.getNumSamples(); ++sample)
		{
			const auto drive = gainFromDb(driveTransition.getNextValue());
			for (std::size_t channel = 0; channel < oversampled.getNumChannels(); ++channel)
			{
				auto& input = oversampled.getChannelPointer(channel)[sample];
				preampLow[channel] += lowCoefficient * (input - preampLow[channel]);
				const auto driven = std::tanh((input + 0.15f * (input - preampLow[channel])) * drive);
				preampHigh[channel] += highCoefficient * (driven - preampHigh[channel]);
				input = preampHigh[channel];
			}
		}
		auto preampOutput = juce::dsp::AudioBlock<float>(block);
		oversampling.processSamplesDown(preampOutput);
		autoGain.process(juce::dsp::AudioBlock<const float>(referenceBuffer.getArrayOfReadPointers(), 2,
			static_cast<std::size_t>(samples)),
			juce::dsp::AudioBlock<float>(block), autoGainParameter->load() >= 0.5f);

		for (int sample = 0; sample < samples; ++sample)
		{
			const auto fastTarget = autoDetector.advance(referenceBuffer.getSample(0, sample), referenceBuffer.getSample(1, sample));
			autoTargetFast.store(fastTarget, std::memory_order_relaxed);
			const std::array input { block.getSample(0, sample), block.getSample(1, sample) };
			auto output = engines[activeEngine].process(input, fastTarget);
			if (switchingModel)
			{
				const auto incoming = engines[1 - activeEngine].process(input, fastTarget);
				if (modelWarmup > 0) --modelWarmup;
				else
				{
					const auto duration = std::max(1, static_cast<int>(std::lround(sampleRateHz * 0.05)));
					const auto blend = static_cast<float>(++modelFade) / static_cast<float>(duration);
					for (std::size_t channel = 0; channel < 2; ++channel)
						output[channel] += blend * (incoming[channel] - output[channel]);
					if (modelFade >= duration)
					{
						activeEngine = 1 - activeEngine;
						switchingModel = false;
						activeModel.store(static_cast<int>(engines[activeEngine].getModel()));
					}
				}
			}
			for (std::size_t channel = 0; channel < 2; ++channel)
				output[channel] = dcBlockers[channel].processSample(output[channel]);
			output = RotaryEngine::applyWidth(output, widthTransition.getNextValue());
			for (int channel = 0; channel < 2; ++channel)
				block.setSample(channel, sample, output[static_cast<std::size_t>(channel)]);
		}
		const auto speeds = engines[activeEngine].speeds();
		modelPending.store(switchingModel || requestedModel != engines[activeEngine].getModel());
		hornRpm.store(speeds[0]);
		drumRpm.store(speeds[1]);
		auto wetBlock = juce::dsp::AudioBlock<float>(block);
		dryWetMixer.mixWetSamples(wetBlock);
		for (int sample = 0; sample < samples; ++sample)
		{
			const auto outputGain = gainFromDb(outputTransition.getNextValue());
			const auto bypassBlend = bypassTransition.getNextValue();
			for (int channel = 0; channel < 2; ++channel)
			{
				const auto wet = block.getSample(channel, sample) * outputGain;
				const auto raw = bypassBuffer.getSample(channel, sample);
				block.setSample(channel, sample, (1.0f - bypassBlend) * wet + bypassBlend * raw);
			}
		}
	}
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
	return new PluginEditor(*this);
}

juce::AudioProcessorParameter* PluginProcessor::getBypassParameter() const
{
	return parameterState.getParameter(parameters::bypass);
}

void PluginProcessor::getStateInformation(juce::MemoryBlock& destination)
{
	stateManager.getMetadata().setProperty("vektPresetSelection", presetSession.selectionState(), nullptr);
	juce::MemoryOutputStream stream(destination, false);
	stateManager.createState().writeToStream(stream);
}

void PluginProcessor::setStateInformation(const void* data, int size)
{
	if (auto restored = juce::ValueTree::readFromData(data, static_cast<size_t>(size)); restored.isValid())
	{
		auto sound = restored.hasType(parameters::stateType) ? restored : restored.getChildWithName(parameters::stateType);
		if (sound.isValid())
			for (const auto* identifier : { parameters::cabinetModel, parameters::brake, parameters::stereoWidth,
				parameters::manualSpeedEnabled, parameters::speedPosition })
				if (!sound.getChildWithProperty("id", identifier).isValid())
				{
					auto* parameter = parameterState.getParameter(identifier);
					juce::ValueTree value("PARAM");
					value.setProperty("id", identifier, nullptr);
					value.setProperty("value", parameter->convertFrom0to1(parameter->getDefaultValue()), nullptr);
					sound.appendChild(value, nullptr);
				}
		if (stateManager.restoreState(restored) && stateManager.getMetadata().hasProperty("vektPresetSelection"))
		{
			presetSession.clear();
			juce::ignoreUnused(presetSession.restoreSelection(
				stateManager.getMetadata().getProperty("vektPresetSelection").toString()));
		}
	}
}

presets::Preset PluginProcessor::createPreset(const juce::String& name) const
{
	return presets::PresetSchema::create(parameters::presetProductIdentifier, name,
		parameterState, parameters::soundParameterIds);
}

juce::Result PluginProcessor::applyPreset(const presets::Preset& preset)
{
	if (const auto result = validatePresetSound(preset); result.failed())
		return result;
	undoManager.beginNewTransaction("Load preset: " + preset.name);
	const auto result = presets::PresetSchema::apply(preset,
		parameters::presetProductIdentifier, parameterState, parameters::soundParameterIds, &undoManager);
	if (result.wasOk())
		presetSession.clear();
	return result;
}

juce::Result PluginProcessor::configureUserPresetDirectory(const juce::File& directory)
{
	if (directory == juce::File {})
		return juce::Result::fail("User preset directory is empty");
	userPresetRepository = std::make_unique<presets::FilePresetRepository>(directory);
	presetCatalog.setUserRepository(userPresetRepository.get());
	return juce::Result::ok();
}

juce::Result PluginProcessor::validatePresetSound(const presets::Preset& preset) const
{
	if (preset.soundSchemaVersion != 1)
		return juce::Result::fail("Unsupported Glimmer preset sound schema");
	return presets::PresetSchema::validate(preset, parameters::presetProductIdentifier,
		parameterState, parameters::soundParameterIds);
}

bool PluginProcessor::matchesPresetSound(const presets::Preset& preset) const
{
	return validatePresetSound(preset).wasOk()
		&& presets::PresetSchema::matches(preset, parameters::presetProductIdentifier,
			parameterState, parameters::soundParameterIds);
}

std::array<float, 2> PluginProcessor::consumeInputPeaks() noexcept { return inputMeter.consumePeaks(); }
std::array<float, 2> PluginProcessor::consumeOutputPeaks() noexcept { return outputMeter.consumePeaks(); }

dsp::OversamplingQuality PluginProcessor::getActiveQuality() const noexcept
{
	return oversampling.getActiveQuality();
}

bool PluginProcessor::hasPendingQualityChange() const noexcept
{
	return qualityChangePending.load();
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
	auto playing = false;
	if (const auto* playHead = getPlayHead())
		if (const auto position = playHead->getPosition())
			playing = position->getIsPlaying();
	transportPlaying.store(playing);
}

void PluginProcessor::applyPendingQualityChange()
{
	if (!qualityChangePending.load() || !prepared.load() || transportPlaying.load())
		return;
	const auto quality = isNonRealtime()
		? parameters::offlineQualityFrom(requestedOfflineOversampling.load())
		: parameters::trackingQualityFrom(requestedTrackingOversampling.load());
	if (quality != oversampling.getActiveQuality())
	{
		oversampling.activate(quality);
		updateLatency();
		bypassDelay.reset();
		dryWetMixer.reset();
		driveTransition.prepare(sampleRateHz * static_cast<double>(oversampling.getActiveFactor()));
		driveTransition.setCurrentAndTargetValue(preampDriveParameter->load());
		preampLow = preampHigh = {};
		autoGain.reset();
		for (auto& blocker : dcBlockers) blocker.reset();
	}
	qualityChangePending.store(false);
}

std::atomic<float>* PluginProcessor::requireParameter(juce::AudioProcessorValueTreeState& state, const char* identifier)
{
	auto* parameter = state.getRawParameterValue(identifier);
	jassert(parameter != nullptr);
	return parameter;
}
}
