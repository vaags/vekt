#pragma once

#include "Parameters.h"
#include "StaticAutoGain.h"

#include <vekt/dsp/DcBlocker.h>
#include <vekt/dsp/LatencyAlignedBypass.h>
#include <vekt/dsp/LatencyAlignedMixer.h>
#include <vekt/dsp/MatchedToneStage.h>
#include <vekt/dsp/OversamplingBank.h>
#include <vekt/dsp/TanhStage.h>
#include <vekt/presets/FilePresetRepository.h>
#include <vekt/presets/Preset.h>
#include <vekt/presets/PresetCatalog.h>
#include <vekt/state/StateManager.h>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>

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
	[[nodiscard]] juce::Result saveUserPreset(
		const juce::String& name,
		presets::PresetSaveMode mode = presets::PresetSaveMode::createOnly);
	[[nodiscard]] juce::Result removeUserPreset(const juce::String& name);
	[[nodiscard]] juce::Result loadPreset(std::size_t index);
	[[nodiscard]] juce::Result loadNextPreset();
	[[nodiscard]] juce::Result loadPreviousPreset();
	[[nodiscard]] const std::vector<presets::PresetEntry>& getPresetEntries() const noexcept;
	[[nodiscard]] std::optional<std::size_t> getCurrentPresetIndex() const noexcept;
	[[nodiscard]] bool isCurrentPresetModified() const;
	[[nodiscard]] juce::AudioProcessorValueTreeState& getParameters() noexcept;
	[[nodiscard]] juce::UndoManager& getUndoManager() noexcept;
	[[nodiscard]] juce::ValueTree& getProjectMetadata() noexcept;
	[[nodiscard]] dsp::OversamplingQuality getActiveQuality() const noexcept;
	[[nodiscard]] bool hasPendingQualityChange() const noexcept;
	void applyPendingQualityChange();

private:
	static constexpr auto transportPollIntervalMs = 250;

	[[nodiscard]] static std::atomic<float>* requireParameter(
		juce::AudioProcessorValueTreeState& state, const char* identifier);
	static void assertMessageThread();
	void parameterChanged(const juce::String& parameterId, float newValue) override;
	void handleAsyncUpdate() override;
	void timerCallback() override;
	void observeTransport() noexcept;
	void processPreparedBlocks(
		juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, bool bypassed);
	void processBypassedBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);
	void processEffectBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);
	void restoreCurrentProgramFromMetadata();

	juce::UndoManager undoManager;
	juce::AudioProcessorValueTreeState parameterState;
	state::StateManager stateManager;
	std::unique_ptr<presets::FilePresetRepository> userPresetRepository;
	presets::PresetCatalog presetCatalog;
	std::optional<std::size_t> currentPresetIndex;
	std::optional<presets::Preset> currentPresetSnapshot;
	int currentProgram {};

	std::atomic<float>* inputGainParameter;
	std::atomic<float>* driveParameter;
	std::atomic<float>* toneParameter;
	std::atomic<float>* biasParameter;
	std::atomic<float>* autoGainParameter;
	std::atomic<float>* bypassParameter;
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
	int maximumPreparedBlockSize {};

	dsp::OversamplingBank<float> oversampling { 2 };
	dsp::LatencyAlignedBypass<float> bypassDelay;
	dsp::LatencyAlignedMixer<float> dryWetMixer;
	dsp::MatchedToneStage<float> toneStage;
	dsp::TanhStage<float> tanhStage;
	std::array<dsp::DcBlocker<float>, 2> dcBlockers;
	StaticAutoGain<float> autoGain;
	juce::dsp::Gain<float> inputGain;
	juce::dsp::Gain<float> outputGain;
	juce::AudioBuffer<float> bypassScratch;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};
}
