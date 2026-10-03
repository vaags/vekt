#pragma once

#include <vekt/glimmer/Parameters.h>

#include "AutoSpeedDetector.h"
#include "RotaryEngine.h"

#include <vekt/dsp/AdaptiveAutoGain.h>
#include <vekt/dsp/DcBlocker.h>
#include <vekt/dsp/LatencyAlignedBypass.h>
#include <vekt/dsp/LatencyAlignedMixer.h>
#include <vekt/dsp/OversamplingBank.h>
#include <vekt/dsp/ScopeTap.h>
#include <vekt/dsp/StereoPeakMeter.h>
#include <vekt/plugin_support/PresetHost.h>
#include <vekt/plugin_support/QualitySelection.h>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <memory>

namespace vekt::glimmer
{
class PluginProcessor final : public juce::AudioProcessor
{
public:
	PluginProcessor();
	~PluginProcessor() override;

	void prepareToPlay(double sampleRate, int maximumBlockSize) override;
	void releaseResources() override;
	bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
	void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override;
	void processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override;
	using AudioProcessor::processBlock;
	using AudioProcessor::processBlockBypassed;

	[[nodiscard]] juce::AudioProcessorEditor* createEditor() override;
	[[nodiscard]] bool hasEditor() const override { return true; }
	[[nodiscard]] const juce::String getName() const override { return parameters::productName; }
	[[nodiscard]] bool acceptsMidi() const override { return false; }
	[[nodiscard]] bool producesMidi() const override { return false; }
	[[nodiscard]] bool isMidiEffect() const override { return false; }
	[[nodiscard]] double getTailLengthSeconds() const override { return 0.5; }
	[[nodiscard]] juce::AudioProcessorParameter* getBypassParameter() const override;

	int getNumPrograms() override;
	int getCurrentProgram() override;
	void setCurrentProgram(int index) override;
	const juce::String getProgramName(int index) override;
	void changeProgramName(int, const juce::String&) override {}
	void getStateInformation(juce::MemoryBlock& destination) override;
	void setStateInformation(const void* data, int size) override;
	[[nodiscard]] presets::Preset createPreset(const juce::String& name) const;
	[[nodiscard]] juce::Result applyPreset(const presets::Preset& preset);
	[[nodiscard]] juce::Result configureUserPresetDirectory(const juce::File& directory);
	[[nodiscard]] presets::PresetSession& getPresetSession() noexcept { return presetHost.session(); }
	[[nodiscard]] juce::Result loadNextPreset();
	[[nodiscard]] juce::Result loadPreviousPreset();

	[[nodiscard]] std::array<float, 2> consumeInputPeaks() noexcept;
	[[nodiscard]] std::array<float, 2> consumeOutputPeaks() noexcept;
	// The output after gain, for the editor's oscilloscope.
	[[nodiscard]] const dsp::ScopeTap& getOutputScope() const noexcept { return outputScope; }
	[[nodiscard]] juce::AudioProcessorValueTreeState& getParameters() noexcept { return parameterState; }
	[[nodiscard]] juce::UndoManager& getUndoManager() noexcept { return undoManager; }
	[[nodiscard]] bool isAutoTargetFast() const noexcept { return autoTargetFast.load(); }
	[[nodiscard]] dsp::OversamplingQuality getActiveQuality() const noexcept;
	[[nodiscard]] bool hasPendingQualityChange() const noexcept;
	[[nodiscard]] int getActiveModel() const noexcept { return activeModel.load(); }
	[[nodiscard]] bool hasPendingModelChange() const noexcept { return modelPending.load(); }
	[[nodiscard]] std::array<float, 2> getRotorSpeeds() const noexcept { return { hornRpm.load(), drumRpm.load() }; }

private:
	void process(juce::AudioBuffer<float>& buffer, bool bypassed);
	void applyPendingQualityChange();
	void updateLatency();
	[[nodiscard]] RotarySettings rotarySettings() const noexcept;
	[[nodiscard]] juce::Result validatePresetSound(const presets::Preset& preset) const;
	[[nodiscard]] bool matchesPresetSound(const presets::Preset& preset) const;

	juce::UndoManager undoManager;
	juce::AudioProcessorValueTreeState parameterState;
	plugin_support::PresetHost presetHost;
	plugin_support::QualitySelection qualitySelection;
	std::atomic<float>* inputGainParameter;
	std::atomic<float>* preampDriveParameter;
	std::atomic<float>* balanceParameter;
	std::atomic<float>* micAngleParameter;
	std::atomic<float>* micDistanceParameter;
	std::atomic<float>* slowSpeedParameter;
	std::atomic<float>* fastSpeedParameter;
	std::atomic<float>* accelerationParameter;
	std::atomic<float>* decelerationParameter;
	std::atomic<float>* hornToneParameter;
	std::atomic<float>* drumToneParameter;
	std::atomic<float>* speedModeParameter;
	std::atomic<float>* sensitivityParameter;
	std::atomic<float>* autoGainParameter;
	std::atomic<float>* bypassParameter;
	std::atomic<float>* mixParameter;
	std::atomic<float>* outputGainParameter;
	std::atomic<float>* modelParameter;
	std::atomic<float>* brakeParameter;
	std::atomic<float>* widthParameter;
	std::atomic<float>* manualParameter;
	std::atomic<float>* positionParameter;

	std::array<RotaryEngine, 2> engines;
	std::size_t activeEngine {};
	int modelWarmup {};
	int modelFade {};
	bool switchingModel {};
	std::atomic<int> activeModel {};
	std::atomic<bool> modelPending {};
	std::atomic<float> hornRpm {};
	std::atomic<float> drumRpm {};
	dsp::ControlTransition<float> inputTransition;
	dsp::ControlTransition<float> outputTransition;
	dsp::ControlTransition<float> driveTransition;
	dsp::ControlTransition<float> widthTransition;
	dsp::ControlTransition<float> bypassTransition;
	bool hasProcessed {};
	std::array<float, 2> preampLow {};
	std::array<float, 2> preampHigh {};
	AutoSpeedDetector autoDetector;
	std::array<dsp::DcBlocker<float>, 2> dcBlockers;
	dsp::AdaptiveAutoGain<float> autoGain;
	dsp::OversamplingBank<float> oversampling { 2 };
	dsp::LatencyAlignedBypass<float> bypassDelay;
	dsp::LatencyAlignedMixer<float> dryWetMixer;
	dsp::StereoPeakMeter inputMeter;
	dsp::StereoPeakMeter outputMeter;
	dsp::ScopeTap outputScope;
	juce::AudioBuffer<float> referenceBuffer;
	juce::AudioBuffer<float> bypassBuffer;
	double sampleRateHz { 48'000.0 };
	int maximumBlockSize {};
	int micLatencySamples {};
	std::atomic<bool> autoTargetFast {};
	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};
}
