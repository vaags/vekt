#pragma once

#include <vekt/flint/Models.h>
#include <vekt/flint/Parameters.h>

#include <vekt/dsp/OversamplingBank.h>
#include <vekt/dsp/ScopeTap.h>
#include <vekt/dsp/StereoPeakMeter.h>
#include <vekt/plugin_support/PresetHost.h>
#include <vekt/plugin_support/QualitySelection.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>

namespace vekt::flint
{
class FlintEngineHost;
struct FlintParameters;

// Flint's processor composes and holds no DSP (docs/FLINT_VALIDATION.md, Signal Path): parameters, MIDI split at event
// positions, the song position and hit hash, quality, presets and the seed; FlintEngineHost renders the sound.
class PluginProcessor final : public juce::AudioProcessor
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
	[[nodiscard]] const juce::String getName() const override { return parameters::productName; }
	[[nodiscard]] bool acceptsMidi() const override { return true; }
	[[nodiscard]] bool producesMidi() const override { return false; }
	[[nodiscard]] bool isMidiEffect() const override { return false; }
	[[nodiscard]] double getTailLengthSeconds() const override { return 0.0; }

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

	// Editor actions, message thread. A Mode change also sets the shared controls to the mode's start values, as one
	// undo step (A31).
	void selectMode(Mode mode);
	void newSeed();
	[[nodiscard]] std::uint64_t getSeed() const noexcept { return seed.load(); }

	[[nodiscard]] juce::AudioProcessorValueTreeState& getParameters() noexcept { return parameterState; }
	[[nodiscard]] juce::UndoManager& getUndoManager() noexcept { return undoManager; }
	[[nodiscard]] const dsp::ScopeTap& getOutputScope() const noexcept { return outputScope; }
	[[nodiscard]] std::array<float, 2> consumeOutputPeaks() noexcept { return outputMeter.consumePeaks(); }
	[[nodiscard]] dsp::OversamplingQuality getActiveQuality() const noexcept { return qualitySelection.active(); }
	// Whether the engine host sleeps: nothing sounds and every block is exact zeros (tests, the editor).
	[[nodiscard]] bool isSilent() const noexcept { return silent.load(); }

	inline static constexpr auto seedProperty = "flintSeed";

private:
	struct CachedParameters;
	[[nodiscard]] FlintParameters snapshot() const noexcept;
	void applyPendingQualityChange() noexcept;
	void configureInternalRate() noexcept;
	void readTransport() noexcept;
	void handleMidi(const juce::MidiMessage& message, int samplePosition) noexcept;
	void render(juce::AudioBuffer<float>& buffer, int start, int count) noexcept;
	void storeSeed(std::uint64_t value);
	[[nodiscard]] juce::Result validatePresetSound(const presets::Preset& preset) const;
	[[nodiscard]] bool matchesPresetSound(const presets::Preset& preset) const;

	juce::UndoManager undoManager;
	juce::AudioProcessorValueTreeState parameterState;
	plugin_support::PresetHost presetHost;
	plugin_support::QualitySelection qualitySelection;
	std::unique_ptr<CachedParameters> cached;
	std::unique_ptr<FlintEngineHost> engineHost;
	dsp::OversamplingBank<float> oversampling { 2 }; // stereo
	dsp::StereoPeakMeter outputMeter;
	dsp::ScopeTap outputScope;

	double sampleRateHz { 44'100.0 };
	int preparedBlockSize {};
	std::atomic<std::uint64_t> seed {};
	std::uint64_t stoppedCounter {};
	std::optional<double> blockStartBeats; // the song position at the block's start, while the transport plays
	double samplesPerBeat { 22'050.0 };
	int lastHitSample { -1 };
	std::uint32_t hitOrder {};
	std::atomic<bool> presetLoading {};
	std::atomic<bool> presetLoaded {};
	std::atomic<bool> silent { true };
};
}
