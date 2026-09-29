#pragma once

#include <vekt/mono/Parameters.h>
#include <vekt/dsp/OversamplingBank.h>
#include <vekt/dsp/StereoPeakMeter.h>
#include <vekt/presets/FilePresetRepository.h>
#include <vekt/presets/PresetCatalog.h>
#include <vekt/presets/PresetSession.h>
#include <vekt/state/StateManager.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace vekt::mono
{
struct MonoVoiceSettings;
class MonoVoice;
class MonoRenderWorkers;
class WorkgroupMailbox;
class LfoClock;

class PluginProcessor final : public juce::AudioProcessor, private juce::AudioProcessorValueTreeState::Listener,
	private juce::AsyncUpdater
{
public:
	PluginProcessor();
	~PluginProcessor() override;

	void prepareToPlay(double sampleRate, int maximumBlockSize) override;
	void releaseResources() override;
	// Helper threads join the host's audio workgroup (macOS) so they are scheduled like the audio thread.
	void audioWorkgroupContextChanged(const juce::AudioWorkgroup& workgroup) override;
	// Helper render threads running (0 until Multicore is first switched on). For tests and diagnostics.
	[[nodiscard]] int getRenderHelperCount() const noexcept;
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
	[[nodiscard]] int getActiveQuality() const noexcept { return activeQuality; }
	// Voices currently producing sound, including release tails. Call from the audio thread or while stopped.
	[[nodiscard]] int getSoundingVoiceCount() const noexcept;
	struct CoupledWorkSnapshot
	{
		std::uint64_t samples {}, iterations {}, lineSearchTrials {}, unconverged {}, nonFinite {};
	};
	[[nodiscard]] CoupledWorkSnapshot coupledWorkSnapshot() const noexcept;
	// The SVF's solver work (ADR 0006), summed over voices like coupledWorkSnapshot.
	struct SvfWorkSnapshot
	{
		std::uint64_t samples {}, iterations {}, fallbackSteps {}, unconverged {}, nonFinite {};
		int maximumIterations {};
		double maximumResidual {};
	};
	[[nodiscard]] SvfWorkSnapshot svfWorkSnapshot() const noexcept;
	[[nodiscard]] std::array<float, 2> consumeOutputPeaks() noexcept { return outputMeter.consumePeaks(); }
	// Latest output of each LFO on the most recently started sounding voice; 0 while silent. For display only.
	[[nodiscard]] float getLfoDisplayValue(std::size_t index) const noexcept { return lfoDisplayValues[index].load(std::memory_order_relaxed); }
	// Sounding voices (including release tails) after the latest block. Safe from any thread; for display.
	[[nodiscard]] int getSoundingVoiceDisplay() const noexcept { return soundingVoiceDisplay.load(std::memory_order_relaxed); }
	// Highest mod wheel or aftertouch amount currently applied (0..1). For display only.
	[[nodiscard]] float getVibratoControlDisplay() const noexcept { return vibratoControlDisplay.load(std::memory_order_relaxed); }

private:
	[[nodiscard]] float value(const char* identifier) const noexcept;
	[[nodiscard]] MonoVoiceSettings snapshotSettings() const;
	void handleMidi(const juce::MidiMessage& message);
	void noteOn(int channel, int note, float velocity);
	void noteOff(int channel, int note);
	void allNotesOff(int channel, bool immediate);
	void releaseSustainedNotes(int channel);
	void resetPlayingState();
	void render(juce::AudioBuffer<float>& buffer, int startSample, int numberOfSamples);
	// Renders one work unit (a group of voices sharing the batched ladder solve) over the current segment.
	void renderUnit(int unit) noexcept;
	static void renderUnitJob(void* processor, int unit) noexcept;
	void parameterChanged(const juce::String& identifier, float newValue) override;
	void handleAsyncUpdate() override;
	void ensureRenderWorkers();
	void applyConfigurationChanges();
	void readTransport();
	void configureQuality(int quality);
	[[nodiscard]] juce::Result validatePresetSound(const presets::Preset& preset) const;
	[[nodiscard]] juce::Result applyPreset(const presets::Preset& preset);
	[[nodiscard]] bool matchesPresetSound(const presets::Preset& preset) const;
	[[nodiscard]] juce::Result loadAdjacentPreset(bool next);
	[[nodiscard]] MonoVoice& findVoiceForNote(int channel, int note);
	[[nodiscard]] MonoVoice& monoVoiceForChannel(int channel);
	void retargetMonophonicVoice(int channel, bool retrigger);
	[[nodiscard]] int activeVoiceLimit() const noexcept;

	juce::UndoManager undoManager;
	juce::AudioProcessorValueTreeState parameterState;
	state::StateManager stateManager;
	std::unique_ptr<presets::FilePresetRepository> userPresetRepository;
	presets::PresetCatalog presetCatalog;
	presets::PresetSession presetSession;
	std::array<std::unique_ptr<MonoVoice>, 16> voices;
	std::array<bool, 16> sustainByChannel {};
	std::array<float, 16> pitchBendByChannel {};
	struct HeldNote
	{
		int note {};
		float velocity {};
	};
	std::array<std::vector<HeldNote>, 16> heldNotesByChannel;
	dsp::OversamplingBank<float> oversampling { 2 };
	std::array<std::unique_ptr<LfoClock>, 2> lfoClocks;
	std::array<std::atomic<float>, 2> lfoDisplayValues {};
	std::unique_ptr<LfoClock> vibratoClock;
	std::array<float, 16> modWheelByChannel {}, pressureByChannel {};
	std::atomic<float> vibratoControlDisplay {};
	std::atomic<int> soundingVoiceDisplay {};
	double transportBpm { 120.0 };
	std::optional<double> transportPpq;
	dsp::StereoPeakMeter outputMeter;
	std::atomic<bool> pendingPresetReset {};
	int activeVoiceCount { 8 };
	int activeQuality {};
	std::uint64_t noteAge {};
	double sampleRateHz { 48'000.0 };
	// Multicore: the pool is created on the message thread the first time Multicore is on, then kept.
	// Created on non-audio threads only (prepareToPlay, the message thread); the mutex covers check and create.
	std::mutex renderWorkerCreation;
	// The helpers' real-time timing, written by prepareToPlay and read by pool creation on another thread.
	std::atomic<int> helperBlockSize { 512 };
	std::atomic<double> helperSampleRate { 48'000.0 };
	std::unique_ptr<MonoRenderWorkers> renderWorkerPool;
	std::atomic<MonoRenderWorkers*> renderWorkers {};
	// Host workgroup: published without waiting from the render thread; a value the mailbox could not take
	// (a helper was reading) stays staged, touched only by that thread, and is retried at the next block.
	std::unique_ptr<WorkgroupMailbox> workgroupMailbox;
	juce::AudioWorkgroup stagedWorkgroup;
	bool workgroupStaged {};
	// One render segment's shared inputs, precomputed so work units render independently.
	struct RenderSegment
	{
		const MonoVoiceSettings* settings {};
		std::array<float, 16> channelControl {};
		int samples {}, units {};
		std::array<std::array<std::uint8_t, 4>, 16> unitVoices {};
		std::array<int, 16> unitVoiceCount {};
	} segment;
	std::size_t unitStride {};
	std::vector<double> lfoPositionBuffer; // two per sample
	std::vector<float> vibratoBuffer, unitBuffer; // unitBuffer: [unit][left, right][sample]
	int preparedBlockSize { 1 };
	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};
}
