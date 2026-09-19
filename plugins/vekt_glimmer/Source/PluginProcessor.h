#pragma once

#include <vekt/glimmer/Parameters.h>

#include "AutoSpeedDetector.h"
#include "RotorMotion.h"

#include <vekt/dsp/AdaptiveAutoGain.h>
#include <vekt/dsp/DcBlocker.h>
#include <vekt/dsp/LatencyAlignedBypass.h>
#include <vekt/dsp/LatencyAlignedMixer.h>
#include <vekt/dsp/OversamplingBank.h>
#include <vekt/dsp/StereoPeakMeter.h>
#include <vekt/presets/FilePresetRepository.h>
#include <vekt/presets/PresetCatalog.h>
#include <vekt/presets/PresetSession.h>
#include <vekt/state/StateManager.h>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <memory>

namespace vekt::glimmer
{
class PluginProcessor final : public juce::AudioProcessor,
	private juce::AudioProcessorValueTreeState::Listener
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
	[[nodiscard]] const juce::String getName() const override { return "Vekt Glimmer"; }
	[[nodiscard]] bool acceptsMidi() const override { return false; }
	[[nodiscard]] bool producesMidi() const override { return false; }
	[[nodiscard]] bool isMidiEffect() const override { return false; }
	[[nodiscard]] double getTailLengthSeconds() const override { return 0.0; }
	[[nodiscard]] juce::AudioProcessorParameter* getBypassParameter() const override;

	int getNumPrograms() override { return 1; }
	int getCurrentProgram() override { return 0; }
	void setCurrentProgram(int) override {}
	const juce::String getProgramName(int) override { return {}; }
	void changeProgramName(int, const juce::String&) override {}
	void getStateInformation(juce::MemoryBlock& destination) override;
	void setStateInformation(const void* data, int size) override;
	[[nodiscard]] presets::Preset createPreset(const juce::String& name) const;
	[[nodiscard]] juce::Result applyPreset(const presets::Preset& preset);
	[[nodiscard]] juce::Result configureUserPresetDirectory(const juce::File& directory);
	[[nodiscard]] presets::PresetSession& getPresetSession() noexcept { return presetSession; }

	[[nodiscard]] std::array<float, 2> consumeInputPeaks() noexcept;
	[[nodiscard]] std::array<float, 2> consumeOutputPeaks() noexcept;
	[[nodiscard]] juce::AudioProcessorValueTreeState& getParameters() noexcept { return parameterState; }
	[[nodiscard]] juce::UndoManager& getUndoManager() noexcept { return undoManager; }
	[[nodiscard]] bool isAutoTargetFast() const noexcept { return autoTargetFast.load(); }
	[[nodiscard]] dsp::OversamplingQuality getActiveQuality() const noexcept;
	[[nodiscard]] bool hasPendingQualityChange() const noexcept;

private:
	[[nodiscard]] static std::atomic<float>* requireParameter(
		juce::AudioProcessorValueTreeState& state, const char* identifier);
	void process(juce::AudioBuffer<float>& buffer, bool bypassed);
	void parameterChanged(const juce::String& parameterId, float newValue) override;
	void observeTransport() noexcept;
	void applyPendingQualityChange();
	[[nodiscard]] juce::Result validatePresetSound(const presets::Preset& preset) const;
	[[nodiscard]] bool matchesPresetSound(const presets::Preset& preset) const;

	juce::UndoManager undoManager;
	juce::AudioProcessorValueTreeState parameterState;
	state::StateManager stateManager;
	std::unique_ptr<presets::FilePresetRepository> userPresetRepository;
	presets::PresetCatalog presetCatalog;
	presets::PresetSession presetSession;
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
	std::atomic<float>* trackingOversamplingParameter;
	std::atomic<float>* offlineOversamplingParameter;
	std::atomic<float> requestedTrackingOversampling { 2.0f };
	std::atomic<float> requestedOfflineOversampling { 4.0f };
	std::atomic<bool> qualityChangePending {};
	std::atomic<bool> transportPlaying {};
	std::atomic<bool> prepared {};

	RotorMotion hornMotion;
	RotorMotion drumMotion;
	AutoSpeedDetector autoDetector;
	juce::dsp::LinkwitzRileyFilter<float> crossover;
	std::array<dsp::DcBlocker<float>, 2> dcBlockers;
	dsp::AdaptiveAutoGain<float> autoGain;
	dsp::OversamplingBank<float> oversampling { 2 };
	dsp::LatencyAlignedBypass<float> bypassDelay;
	dsp::LatencyAlignedMixer<float> dryWetMixer;
	using MicDelay = juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd>;
	std::array<MicDelay, 2> hornMicDelays;
	std::array<MicDelay, 2> drumMicDelays;
	dsp::StereoPeakMeter inputMeter;
	dsp::StereoPeakMeter outputMeter;
	juce::AudioBuffer<float> referenceBuffer;
	juce::AudioBuffer<float> bypassBuffer;
	double sampleRateHz { 48'000.0 };
	int maximumBlockSize {};
	int micLatencySamples {};
	std::atomic<bool> autoTargetFast {};
	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};
}
