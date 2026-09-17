#pragma once

#include "Parameters.h"

#include <vekt/dsp/DcBlocker.h>
#include <vekt/dsp/LatencyAlignedBypass.h>
#include <vekt/dsp/LatencyAlignedMixer.h>
#include <vekt/dsp/OversamplingBank.h>
#include <vekt/dsp/TanhStage.h>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <cstdint>

namespace vekt::saturator
{
class PluginProcessor final : public juce::AudioProcessor,
							  private juce::AudioProcessorValueTreeState::Listener,
							  private juce::AsyncUpdater,
							  private juce::Timer
{
public:
	PluginProcessor();
	~PluginProcessor() override;

	void prepareToPlay(double sampleRate, int maximumBlockSize) override;
	void releaseResources() override;
	bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
	void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
	void processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
	using AudioProcessor::processBlock;
	using AudioProcessor::processBlockBypassed;

	[[nodiscard]] juce::AudioProcessorEditor* createEditor() override;
	[[nodiscard]] bool hasEditor() const override;
	[[nodiscard]] const juce::String getName() const override;
	[[nodiscard]] bool acceptsMidi() const override;
	[[nodiscard]] bool producesMidi() const override;
	[[nodiscard]] bool isMidiEffect() const override;
	[[nodiscard]] double getTailLengthSeconds() const override;

	int getNumPrograms() override;
	int getCurrentProgram() override;
	void setCurrentProgram(int index) override;
	const juce::String getProgramName(int index) override;
	void changeProgramName(int index, const juce::String& name) override;

	void getStateInformation(juce::MemoryBlock& destination) override;
	void setStateInformation(const void* data, int size) override;

	[[nodiscard]] juce::AudioProcessorValueTreeState& getParameters() noexcept;
	[[nodiscard]] dsp::OversamplingQuality getActiveQuality() const noexcept;
	[[nodiscard]] bool hasPendingQualityChange() const noexcept;
	void applyPendingQualityChange();

private:
	static constexpr auto transportPollIntervalMs = 250;

	[[nodiscard]] static std::atomic<float>* requireParameter(
		juce::AudioProcessorValueTreeState& state, const char* identifier);
	void parameterChanged(const juce::String& parameterId, float newValue) override;
	void handleAsyncUpdate() override;
	void timerCallback() override;
	void observeTransport() noexcept;
	void processEffectBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);

	juce::UndoManager undoManager;
	juce::AudioProcessorValueTreeState parameterState;

	std::atomic<float>* inputGainParameter;
	std::atomic<float>* driveParameter;
	std::atomic<float>* biasParameter;
	std::atomic<float>* mixParameter;
	std::atomic<float>* outputGainParameter;
	std::atomic<float>* oversamplingFactorParameter;
	std::atomic<float>* oversamplingPhaseParameter;
	std::atomic<float> requestedOversamplingFactor { 2.0f };
	std::atomic<float> requestedOversamplingPhase {};
	std::atomic<bool> qualityChangePending {};
	std::atomic<bool> transportPlaying {};
	std::atomic<std::uint64_t> processCounter {};
	std::uint64_t lastObservedProcessCounter {};
	std::atomic<bool> prepared {};

	dsp::OversamplingBank<float> oversampling { 2 };
	dsp::LatencyAlignedBypass<float> bypassDelay;
	dsp::LatencyAlignedMixer<float> dryWetMixer;
	dsp::TanhStage<float> tanhStage;
	std::array<dsp::DcBlocker<float>, 2> dcBlockers;
	juce::dsp::Gain<float> inputGain;
	juce::dsp::Gain<float> outputGain;
	juce::AudioBuffer<float> bypassScratch;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};
}
