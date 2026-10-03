#pragma once

#include <vekt/kobber/Parameters.h>
#include <vekt/dsp/DisplayHistory.h>
#include <vekt/dsp/OversamplingBank.h>
#include <vekt/dsp/ScopeTap.h>
#include <vekt/dsp/StereoPeakMeter.h>
#include <vekt/plugin_support/PresetHost.h>
#include <vekt/plugin_support/QualitySelection.h>

#include "KobberSettingsSnapshot.h"
#include "KobberRenderPlan.h"
#include "KobberVoiceAllocator.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace vekt::kobber
{
struct KobberVoiceSettings;
class KobberVoice;
class KobberRenderWorkers;
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
	[[nodiscard]] const juce::String getName() const override { return parameters::productName; }
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
	[[nodiscard]] presets::PresetSession& getPresetSession() noexcept { return presetHost.session(); }
	[[nodiscard]] juce::Result loadNextPreset();
	[[nodiscard]] juce::Result loadPreviousPreset();
	// The oversampling quality in use. Safe from any thread.
	[[nodiscard]] dsp::OversamplingQuality getActiveQuality() const noexcept { return qualitySelection.active(); }
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
	// The output after Master Output, for the editor's oscilloscope.
	[[nodiscard]] const dsp::ScopeTap& getOutputScope() const noexcept { return outputScope; }
	// Latest output of each LFO on the most recently started sounding voice; 0 while silent. For display only.
	[[nodiscard]] float getLfoDisplayValue(std::size_t index) const noexcept { return lfoDisplayValues[index].load(std::memory_order_relaxed); }
	// Whether a voice is sounding, so getLfoDisplayValue's 0 can be told apart from silence. For display only.
	[[nodiscard]] bool isLfoDisplayActive() const noexcept { return lfoDisplayActive.load(std::memory_order_relaxed); }
	// Both LFOs' outputs on the newest sounding voice at the end of every block, stamped with the block's sample
	// position, for smooth display animation; a frame's tag is 0 while nothing sounds. Read from the message thread only.
	using LfoHistory = dsp::DisplayHistory<2>;
	[[nodiscard]] LfoHistory& getLfoHistory() noexcept { return lfoHistory; }
	// Each LFO's effective rate in Hz (tempo-synced rates follow the host tempo). For display only.
	[[nodiscard]] float getLfoDisplayRate(std::size_t index) const noexcept { return lfoDisplayRates[index].load(std::memory_order_relaxed); }
	// Sounding voices (including release tails) after the latest block. Safe from any thread; for display.
	[[nodiscard]] int getSoundingVoiceDisplay() const noexcept { return soundingVoiceDisplay.load(std::memory_order_relaxed); }
	// The latency of the active quality, published with it. Safe from any thread; for display.
	[[nodiscard]] int getLatencyDisplay() const noexcept { return latencyDisplay.load(std::memory_order_relaxed); }
	// Highest mod wheel, aftertouch or Vibrato Amount currently applied (0..1). For display only.
	[[nodiscard]] float getVibratoControlDisplay() const noexcept { return vibratoControlDisplay.load(std::memory_order_relaxed); }

private:
	[[nodiscard]] static float value(const std::atomic<float>* parameter) noexcept { return parameter->load(); }
	void handleMidi(const juce::MidiMessage& message);
	void resetPlayingState();
	void render(juce::AudioBuffer<float>& buffer, int startSample, int numberOfSamples);
	// Renders one job's voices (sharing one batched filter solve) over the current segment, each into its own buffer.
	void renderJob(int job) noexcept;
	static void renderJobCallback(void* processor, int job) noexcept;
	void parameterChanged(const juce::String& identifier, float newValue) override;
	void handleAsyncUpdate() override;
	void ensureRenderWorkers();
	void applyConfigurationChanges();
	void readTransport();
	void configureQuality(dsp::OversamplingQuality quality);
	[[nodiscard]] juce::Result validatePresetSound(const presets::Preset& preset) const;
	[[nodiscard]] juce::Result applyPreset(const presets::Preset& preset);
	[[nodiscard]] bool matchesPresetSound(const presets::Preset& preset) const;
	[[nodiscard]] KobberVoiceAllocator<KobberVoice>::Rules allocationRules() const noexcept;

	juce::UndoManager undoManager;
	juce::AudioProcessorValueTreeState parameterState;
	plugin_support::PresetHost presetHost;
	plugin_support::QualitySelection qualitySelection;
	KobberParameterValues cached;
	std::array<std::unique_ptr<KobberVoice>, 16> voices;
	std::array<float, 16> pitchBendByChannel {};
	KobberVoiceAllocator<KobberVoice> allocator;
	dsp::OversamplingBank<float> oversampling { 2 };
	std::array<std::unique_ptr<LfoClock>, 2> lfoClocks;
	std::array<std::atomic<float>, 2> lfoDisplayValues {}, lfoDisplayRates {};
	std::atomic<bool> lfoDisplayActive {};
	LfoHistory lfoHistory;
	std::uint64_t renderedSamples {}; // since prepareToPlay, the timeline of lfoHistory
	std::unique_ptr<LfoClock> vibratoClock;
	std::array<float, 16> modWheelByChannel {}, pressureByChannel {};
	std::atomic<float> vibratoControlDisplay {};
	std::atomic<int> soundingVoiceDisplay {}, latencyDisplay {};
	double transportBpm { 120.0 };
	std::optional<double> transportPpq;
	dsp::StereoPeakMeter outputMeter;
	dsp::ScopeTap outputScope;
	std::atomic<bool> pendingPresetReset {};
	int activeVoiceCount { 8 };
	double sampleRateHz { 48'000.0 };
	// Multicore: the pool is created on the message thread the first time Multicore is on, then kept.
	// Created on non-audio threads only (prepareToPlay, the message thread); the mutex covers check and create.
	std::mutex renderWorkerCreation;
	// The helpers' real-time timing, written by prepareToPlay and read by pool creation on another thread.
	std::atomic<int> helperBlockSize { 512 };
	std::atomic<double> helperSampleRate { 48'000.0 };
	std::unique_ptr<KobberRenderWorkers> renderWorkerPool;
	std::atomic<KobberRenderWorkers*> renderWorkers {};
	// Host workgroup: published without waiting from the render thread; a value the mailbox could not take
	// (a helper was reading) stays staged, touched only by that thread, and is retried at the next block.
	std::unique_ptr<WorkgroupMailbox> workgroupMailbox;
	juce::AudioWorkgroup stagedWorkgroup;
	bool workgroupStaged {};
	// One render segment's shared inputs, precomputed so jobs render independently (RenderPlan: units fix the
	// summing order, jobs split the same voices across threads).
	struct RenderSegment
	{
		const KobberVoiceSettings* settings {};
		std::array<float, 16> channelControl {};
		int samples {};
		RenderPlan plan;
	} segment;
	std::size_t voiceStride {};
	std::vector<double> lfoPositionBuffer; // two per sample
	std::vector<float> vibratoBuffer, voiceBuffer; // voiceBuffer: [voice][left, right][sample]
	int preparedBlockSize { 1 };
	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};
}
