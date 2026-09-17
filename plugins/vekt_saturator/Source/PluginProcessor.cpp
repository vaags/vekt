#include "PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <span>

namespace vekt::saturator
{
PluginProcessor::PluginProcessor()
	: AudioProcessor(BusesProperties()
		.withInput("Input", juce::AudioChannelSet::stereo(), true)
		.withOutput("Output", juce::AudioChannelSet::stereo(), true)),
	  parameterState(*this, &undoManager, parameters::stateType, parameters::createLayout()),
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
}

bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
	return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
		&& layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
	if (bypassParameter->load() >= 0.5f)
	{
		processBlockBypassed(buffer, midi);
		return;
	}

	observeTransport();
	bypassDelay.advance(juce::dsp::AudioBlock<const float>(buffer));
	processEffectBlock(buffer, midi);
}

void PluginProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
	observeTransport();
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
int PluginProcessor::getNumPrograms() { return 1; }
int PluginProcessor::getCurrentProgram() { return 0; }
void PluginProcessor::setCurrentProgram(int index) { juce::ignoreUnused(index); }
const juce::String PluginProcessor::getProgramName(int index) { juce::ignoreUnused(index); return {}; }
void PluginProcessor::changeProgramName(int index, const juce::String& name) { juce::ignoreUnused(index, name); }

void PluginProcessor::getStateInformation(juce::MemoryBlock& destination)
{
	auto state = parameterState.copyState();
	state.setProperty("stateVersion", parameters::stateVersion, nullptr);

	if (const auto xml = state.createXml())
		copyXmlToBinary(*xml, destination);
}

void PluginProcessor::setStateInformation(const void* data, int size)
{
	const auto xml = getXmlFromBinary(data, size);
	if (xml == nullptr)
		return;

	auto state = juce::ValueTree::fromXml(*xml);
	if (state.isValid() && state.hasType(parameters::stateType))
		parameterState.replaceState(state);
}

juce::AudioProcessorValueTreeState& PluginProcessor::getParameters() noexcept
{
	return parameterState;
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
