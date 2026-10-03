#pragma once

#include <vekt/rav/Parameters.h>
#include "RavModeStage.h"
#include "RavStageChain.h"

#include <vekt/dsp/AdaptiveAutoGain.h>
#include <vekt/dsp/ControlTransition.h>
#include <vekt/dsp/DcBlocker.h>
#include <vekt/dsp/LatencyAlignedBypass.h>
#include <vekt/dsp/LatencyAlignedMixer.h>
#include <vekt/dsp/MatchedToneStage.h>
#include <vekt/dsp/OversamplingBank.h>
#include <vekt/dsp/ScopeTap.h>
#include <vekt/dsp/StereoPeakMeter.h>
#include <vekt/dsp/TanhStage.h>
#include <vekt/dsp/ThreeBandCrossover.h>
#include <vekt/plugin_support/PresetHost.h>
#include <vekt/plugin_support/QualitySelection.h>
#include <vekt/presets/Preset.h>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>

namespace vekt::rav
{
class PluginProcessor final : public juce::AudioProcessor
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
	[[nodiscard]] juce::AudioProcessorParameter* getBypassParameter() const override;

	int getNumPrograms() override;
	int getCurrentProgram() override;
	void setCurrentProgram(int index) override;
	const juce::String getProgramName(int index) override;
	void changeProgramName(int index, const juce::String& name) override;

	void getStateInformation(juce::MemoryBlock& destination) override;
	void setStateInformation(const void* data, int size) override;

	[[nodiscard]] presets::Preset createPreset(
		const juce::String& name, const juce::NamedValueSet& metadata = {}) const;
	[[nodiscard]] juce::Result applyPreset(const presets::Preset& preset);
	[[nodiscard]] juce::Result configureUserPresetDirectory(const juce::File& directory);
	[[nodiscard]] juce::Result loadNextPreset();
	[[nodiscard]] juce::Result loadPreviousPreset();
	[[nodiscard]] presets::PresetSession& getPresetSession() noexcept { return presetHost.session(); }
	[[nodiscard]] std::array<float, 2> consumeInputPeaks() noexcept;
	[[nodiscard]] std::array<float, 2> consumeOutputPeaks() noexcept;
	// The output after gain, for the editor's oscilloscope.
	[[nodiscard]] const dsp::ScopeTap& getOutputScope() const noexcept { return outputScope; }
	[[nodiscard]] juce::AudioProcessorValueTreeState& getParameters() noexcept;
	[[nodiscard]] juce::UndoManager& getUndoManager() noexcept;
	[[nodiscard]] juce::ValueTree& getProjectMetadata() noexcept;
	[[nodiscard]] RavStageChain::Order getStageOrder() const noexcept;
	[[nodiscard]] bool reorderStage(std::size_t index, int delta) noexcept;
	[[nodiscard]] dsp::OversamplingQuality getActiveQuality() const noexcept;
	[[nodiscard]] bool hasPendingQualityChange() const noexcept;

private:
	static void assertMessageThread();
	void processPreparedBlocks(
		juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, bool bypassed);
	void processBypassedBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);
	void processEffectBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);
	void applyPendingQualityChange();
	[[nodiscard]] juce::Result validatePresetSound(const presets::Preset& preset) const;
	[[nodiscard]] bool matchesPresetSound(const presets::Preset& preset) const;

	juce::UndoManager undoManager;
	juce::AudioProcessorValueTreeState parameterState;
	plugin_support::PresetHost presetHost;
	plugin_support::QualitySelection qualitySelection;

	std::atomic<float>* inputGainParameter;
	std::atomic<float>* driveParameter;
	std::atomic<float>* toneParameter;
	std::atomic<float>* biasParameter;
	std::atomic<float>* autoGainParameter;
	std::atomic<float>* bypassParameter;
	std::atomic<float>* mixParameter;
	std::atomic<float>* outputGainParameter;
	std::atomic<float>* lowBandMixParameter;
	std::atomic<float>* midBandMixParameter;
	std::atomic<float>* highBandMixParameter;
	std::atomic<float>* lowMidCutoffParameter;
	std::atomic<float>* midHighCutoffParameter;
	std::atomic<float>* modeParameter;
	std::atomic<float>* shapeParameter;
	std::atomic<float>* dynamicsParameter;
	std::atomic<float>* textureParameter;
	std::array<std::atomic<float> *, RavStageChain::stageCount> stageEnabledParameters;
	double preparedSampleRate {};
	int maximumPreparedBlockSize {};

	dsp::OversamplingBank<float> oversampling { 2 };
	dsp::LatencyAlignedBypass<float> bypassDelay;
	dsp::LatencyAlignedMixer<float> dryWetMixer;
	dsp::MatchedToneStage<float> toneStage;
	RavStageChain stageChain;
	std::array<std::array<std::array<RavModeStage, RavStageChain::stageCount>, 2>, 3> bandStages;
	dsp::ThreeBandCrossover<float> crossover;
	std::array<juce::AudioBuffer<float>, 3> bandBuffers;
	std::array<juce::AudioBuffer<float>, 3> cleanBandBuffers;
	std::array<dsp::DcBlocker<float>, 2> dcBlockers;
	dsp::AdaptiveAutoGain<float> autoGain;
	std::array<dsp::ControlTransition<float>, 3> bandMixSmoothers;
	std::array<std::array<dsp::ControlTransition<float>, RavStageChain::stageCount>, 3> stageEnableSmoothers;
	dsp::StereoPeakMeter inputMeter;
	dsp::StereoPeakMeter outputMeter;
	dsp::ScopeTap outputScope;
	juce::dsp::Gain<float> inputGain;
	juce::dsp::Gain<float> outputGain;
	juce::AudioBuffer<float> bypassScratch;
	juce::AudioBuffer<float> stageScratch;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};
}
