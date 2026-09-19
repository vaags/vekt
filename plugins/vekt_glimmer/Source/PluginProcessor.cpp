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
constexpr float crossoverHz = 800.0f;
constexpr float drumSpeedRatio = 0.82f;
constexpr float drumInertiaRatio = 1.5f;
constexpr double maximumMicDelaySeconds = 0.012;
constexpr double commonMicDelaySeconds = 0.008;
constexpr double maximumMicDifferentialDelaySeconds = 0.003;

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
	  offlineOversamplingParameter(requireParameter(parameterState, parameters::offlineOversampling))
{
	crossover.setCutoffFrequency(crossoverHz);
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
	crossover.prepare(spec);
	crossover.setCutoffFrequency(crossoverHz);
	crossover.reset();
	oversampling.prepare(static_cast<std::size_t>(maximumBlockSize));
	requestedTrackingOversampling.store(trackingOversamplingParameter->load());
	requestedOfflineOversampling.store(offlineOversamplingParameter->load());
	oversampling.activate(isNonRealtime()
		? parameters::offlineQualityFrom(requestedOfflineOversampling.load())
		: parameters::trackingQualityFrom(requestedTrackingOversampling.load()));
	juce::dsp::ProcessSpec micDelaySpec = spec;
	micDelaySpec.numChannels = 1;
	micLatencySamples = static_cast<int>(std::ceil(maximumMicDelaySeconds * sampleRateHz));
	bypassDelay.prepare(spec, oversampling.getMaximumLatencySamples() + micLatencySamples);
	dryWetMixer.prepare(spec, oversampling.getMaximumLatencySamples() + micLatencySamples);
	dryWetMixer.setRampLength(0.1);
	for (auto* delay : { hornMicDelays.data(), drumMicDelays.data() })
		for (std::size_t channel = 0; channel < 2; ++channel)
		{
			delay[channel].setMaximumDelayInSamples(micLatencySamples);
			delay[channel].prepare(micDelaySpec);
		}
	bypassDelay.setLatency(oversampling.getActiveLatencySamples() + micLatencySamples);
	dryWetMixer.setWetLatency(oversampling.getActiveLatencySamples() + micLatencySamples);
	for (auto& blocker : dcBlockers)
		blocker.prepare(sampleRateHz);
	autoGain.prepare(sampleRateHz);
	hornMotion.prepare(sampleRateHz, slowSpeedParameter->load());
	drumMotion.prepare(sampleRateHz, slowSpeedParameter->load() * drumSpeedRatio);
	autoDetector.prepare(sampleRateHz);
	referenceBuffer.setSize(2, maximumBlockSize, false, false, true);
	bypassBuffer.setSize(2, maximumBlockSize, false, false, true);
	qualityChangePending.store(false);
	prepared.store(true);
	setLatencySamples(oversampling.getActiveLatencySamples() + micLatencySamples);
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
	for (int offset = 0; offset < buffer.getNumSamples(); offset += maximumBlockSize)
	{
		const auto samples = std::min(maximumBlockSize, buffer.getNumSamples() - offset);
		juce::AudioBuffer<float> block(buffer.getArrayOfWritePointers(), 2, offset, samples);
		if (bypassed)
			for (int channel = 0; channel < 2; ++channel)
				bypassBuffer.copyFrom(channel, 0, block, channel, 0, samples);
		else
			bypassDelay.advance(juce::dsp::AudioBlock<const float>(block));

		const auto inputGain = gainFromDb(inputGainParameter->load());
		const auto outputGain = gainFromDb(outputGainParameter->load());
		dryWetMixer.setWetProportion(mixParameter->load() * 0.01f);
		const auto drive = gainFromDb(preampDriveParameter->load());
		const auto hornGain = gainFromDb(hornToneParameter->load());
		const auto drumGain = gainFromDb(drumToneParameter->load());
		const auto balance = juce::jlimit(-1.0f, 1.0f, balanceParameter->load() * 0.01f);
		const auto hornBalance = std::sqrt(0.5f * (1.0f + balance));
		const auto drumBalance = std::sqrt(0.5f * (1.0f - balance));
		const auto angleTurns = micAngleParameter->load() / 360.0f;
		const auto distance = micDistanceParameter->load();
		const auto depth = juce::jmap(distance, 0.3f, 3.0f, 0.68f, 0.2f);
		const auto airGain = juce::jmap(distance, 0.3f, 3.0f, 1.0f, 0.72f);
		const auto differentialDelay = static_cast<float>(juce::jmap(static_cast<double>(distance), 0.3, 3.0,
			0.0003, maximumMicDifferentialDelaySeconds) * sampleRateHz);
		const auto commonDelay = static_cast<float>(commonMicDelaySeconds * sampleRateHz);

		autoDetector.setSensitivity(sensitivityParameter->load() * 0.01f);
		hornMotion.setSpeeds(slowSpeedParameter->load(), fastSpeedParameter->load());
		hornMotion.setTransitionTimes(accelerationParameter->load(), decelerationParameter->load());
		drumMotion.setSpeeds(slowSpeedParameter->load() * drumSpeedRatio,
			fastSpeedParameter->load() * drumSpeedRatio);
		drumMotion.setTransitionTimes(accelerationParameter->load() * drumInertiaRatio,
			decelerationParameter->load() * drumInertiaRatio);
		const auto mode = speedModeFrom(speedModeParameter->load());
		hornMotion.setMode(mode);
		drumMotion.setMode(mode);
		for (int channel = 0; channel < 2; ++channel)
			for (int sample = 0; sample < samples; ++sample)
			{
				const auto input = block.getSample(channel, sample) * inputGain;
				referenceBuffer.setSample(channel, sample, input);
				block.setSample(channel, sample, input);
			}
		dryWetMixer.pushDrySamples(juce::dsp::AudioBlock<const float>(block));
		auto oversampled = oversampling.processSamplesUp(juce::dsp::AudioBlock<const float>(block));
		for (std::size_t channel = 0; channel < oversampled.getNumChannels(); ++channel)
		{
			for (std::size_t sample = 0; sample < oversampled.getNumSamples(); ++sample)
				oversampled.getChannelPointer(channel)[sample] = std::tanh(oversampled.getChannelPointer(channel)[sample] * drive);
		}
			auto preampOutput = juce::dsp::AudioBlock<float>(block);
			oversampling.processSamplesDown(preampOutput);
		autoGain.process(juce::dsp::AudioBlock<const float>(referenceBuffer.getArrayOfReadPointers(), 2,
			static_cast<std::size_t>(samples)),
			juce::dsp::AudioBlock<float>(block), autoGainParameter->load() >= 0.5f);

		for (int sample = 0; sample < samples; ++sample)
		{
			autoTargetFast.store(autoDetector.advance(referenceBuffer.getSample(0, sample), referenceBuffer.getSample(1, sample)), std::memory_order_relaxed);
			hornMotion.setAutoFast(autoTargetFast.load(std::memory_order_relaxed));
			drumMotion.setAutoFast(autoTargetFast.load(std::memory_order_relaxed));
			const auto hornPhase = hornMotion.advance() + angleTurns;
			const auto drumPhase = drumMotion.advance() + angleTurns;

			for (int channel = 0; channel < 2; ++channel)
			{
				float low {};
				float high {};
				crossover.processSample(channel, block.getSample(channel, sample), low, high);
				const auto side = channel == 0 ? 0.0f : 0.25f;
				const auto hornMotionGain = 1.0f - depth * 0.5f
					+ depth * 0.5f * std::cos(2.0f * std::numbers::pi_v<float> * (hornPhase + side));
				const auto drumMotionGain = 1.0f - depth * 0.3f
					+ depth * 0.3f * std::cos(2.0f * std::numbers::pi_v<float> * (drumPhase + side));
				const auto horn = high * hornGain * hornBalance * hornMotionGain * airGain;
				const auto drum = low * drumGain * drumBalance * drumMotionGain;
				const auto hornDelay = commonDelay + differentialDelay
					* std::cos(2.0f * std::numbers::pi_v<float> * (hornPhase + side));
				const auto drumDelay = commonDelay + differentialDelay
					* std::cos(2.0f * std::numbers::pi_v<float> * (drumPhase + side));
				hornMicDelays[static_cast<std::size_t>(channel)].pushSample(0, horn);
				drumMicDelays[static_cast<std::size_t>(channel)].pushSample(0, drum);
				const auto output = hornMicDelays[static_cast<std::size_t>(channel)].popSample(0, hornDelay)
					+ drumMicDelays[static_cast<std::size_t>(channel)].popSample(0, drumDelay);
				block.setSample(channel, sample, dcBlockers[static_cast<std::size_t>(channel)].processSample(output));
			}
		}
		auto wetBlock = juce::dsp::AudioBlock<float>(block);
		dryWetMixer.mixWetSamples(wetBlock);
		for (int channel = 0; channel < 2; ++channel)
			for (int sample = 0; sample < samples; ++sample)
				block.setSample(channel, sample, block.getSample(channel, sample) * outputGain);
		if (bypassed)
		{
			for (int channel = 0; channel < 2; ++channel)
				block.copyFrom(channel, 0, bypassBuffer, channel, 0, samples);
			bypassDelay.processReplacing(juce::dsp::AudioBlock<float>(block));
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
	if (const auto restored = juce::ValueTree::readFromData(data, static_cast<size_t>(size)); restored.isValid())
		if (stateManager.restoreState(restored) && stateManager.getMetadata().hasProperty("vektPresetSelection"))
		{
			presetSession.clear();
			juce::ignoreUnused(presetSession.restoreSelection(
				stateManager.getMetadata().getProperty("vektPresetSelection").toString()));
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
		bypassDelay.setLatency(oversampling.getActiveLatencySamples() + micLatencySamples);
		bypassDelay.reset();
		dryWetMixer.setWetLatency(oversampling.getActiveLatencySamples() + micLatencySamples);
		dryWetMixer.reset();
		autoGain.reset();
		for (auto& blocker : dcBlockers) blocker.reset();
		setLatencySamples(oversampling.getActiveLatencySamples() + micLatencySamples);
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
