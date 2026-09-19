#pragma once

#include <vekt/mono/Parameters.h>
#include <vekt/dsp/OversamplingBank.h>
#include <vekt/presets/FilePresetRepository.h>
#include <vekt/presets/PresetCatalog.h>
#include <vekt/presets/PresetSession.h>
#include <vekt/state/StateManager.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <vector>

namespace vekt::mono
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
	void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
	using AudioProcessor::processBlock;

	[[nodiscard]] juce::AudioProcessorEditor* createEditor() override;
	[[nodiscard]] bool hasEditor() const override { return true; }
	[[nodiscard]] const juce::String getName() const override { return "Vekt Mono"; }
	[[nodiscard]] bool acceptsMidi() const override { return true; }
	[[nodiscard]] bool producesMidi() const override { return false; }
	[[nodiscard]] bool isMidiEffect() const override { return false; }
	[[nodiscard]] double getTailLengthSeconds() const override { return 20.0; }
	int getNumPrograms() override;
	int getCurrentProgram() override;
	void setCurrentProgram(int index) override;
	const juce::String getProgramName(int index) override;
	void changeProgramName(int, const juce::String&) override {}
	void getStateInformation(juce::MemoryBlock& destination) override;
	void setStateInformation(const void* data, int size) override;

	[[nodiscard]] juce::AudioProcessorValueTreeState& getParameters() noexcept { return parameterState; }
	[[nodiscard]] juce::UndoManager& getUndoManager() noexcept { return undoManager; }
	[[nodiscard]] presets::PresetSession& getPresetSession() noexcept { return presetSession; }
	[[nodiscard]] juce::Result loadNextPreset();
	[[nodiscard]] juce::Result loadPreviousPreset();
	[[nodiscard]] bool hasPendingVoiceCountChange() const noexcept { return pendingVoiceCount.load(); }
	[[nodiscard]] bool hasPendingQualityChange() const noexcept { return pendingQuality.load(); }
	[[nodiscard]] int getActiveQuality() const noexcept { return activeQuality; }

private:
	struct Settings;
	class Voice;
	[[nodiscard]] float value(const char* identifier) const noexcept;
	[[nodiscard]] Settings snapshotSettings() const;
	void handleMidi(const juce::MidiMessage& message);
	void noteOn(int channel, int note, float velocity);
	void noteOff(int channel, int note);
	void allNotesOff(int channel, bool immediate);
	void releaseSustainedNotes(int channel);
	void render(juce::AudioBuffer<float>& buffer, int startSample, int numberOfSamples);
	void applyDeferredConfiguration();
	void configureQuality(int quality);
	[[nodiscard]] bool isTransportStopped() const noexcept;
	[[nodiscard]] juce::Result validatePresetSound(const presets::Preset& preset) const;
	[[nodiscard]] juce::Result applyPreset(const presets::Preset& preset);
	[[nodiscard]] bool matchesPresetSound(const presets::Preset& preset) const;
	[[nodiscard]] juce::Result loadAdjacentPreset(bool next);
	[[nodiscard]] Voice& findVoiceForNote(int channel, int note);
	[[nodiscard]] Voice& monoVoiceForChannel(int channel);
	void retargetMonophonicVoice(int channel, bool retrigger);
	[[nodiscard]] int activeVoiceLimit() const noexcept;
	void parameterChanged(const juce::String& parameterId, float) override;

	juce::UndoManager undoManager;
	juce::AudioProcessorValueTreeState parameterState;
	state::StateManager stateManager;
	std::unique_ptr<presets::FilePresetRepository> userPresetRepository;
	presets::PresetCatalog presetCatalog;
	presets::PresetSession presetSession;
	std::array<std::unique_ptr<Voice>, 16> voices;
	std::array<bool, 16> sustainByChannel {};
	std::array<float, 16> pitchBendByChannel {};
	std::array<std::vector<int>, 16> heldNotesByChannel;
	dsp::OversamplingBank<float> oversampling { 2 };
	std::atomic<bool> pendingVoiceCount {};
	std::atomic<bool> pendingQuality {};
	int activeVoiceCount { 8 };
	int requestedVoiceCount { 8 };
	int activeQuality {};
	int requestedQuality {};
	std::uint64_t noteAge {};
	double sampleRateHz { 48'000.0 };
	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};
}