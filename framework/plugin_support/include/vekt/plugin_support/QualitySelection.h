#pragma once

#include <vekt/dsp/OversamplingChoices.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>
#include <optional>

namespace vekt::plugin_support
{
// The oversampling quality a product's Tracking (real-time) and Offline parameters request, and the one active.
// A parameter change from any thread is taken by the audio thread at the start of the next block and applied at
// once, during playback too; the audio may drop at the switch (ADR 0010). The editor reads the active quality from
// any thread. Every product offers the same choices (`dsp::trackingQualityChoices()`, `dsp::offlineQualityChoices()`)
// and activates what this returns.
class QualitySelection final : private juce::AudioProcessorValueTreeState::Listener
{
public:
	// The two non-automatable parameters, for the product's layout; defaults are choice indices.
	[[nodiscard]] static std::unique_ptr<juce::AudioParameterChoice> makeTrackingParameter(
		const char* identifier, int versionHint, int defaultIndex);
	[[nodiscard]] static std::unique_ptr<juce::AudioParameterChoice> makeOfflineParameter(
		const char* identifier, int versionHint, int defaultIndex);

	// The identifiers must outlive this object (products pass their static parameter ID constants).
	QualitySelection(juce::AudioProcessorValueTreeState& parameters, const char* trackingIdentifier,
		const char* offlineIdentifier);
	~QualitySelection() override;

	// prepareToPlay: the quality to activate for this processing mode, now active; drops a pending request.
	[[nodiscard]] dsp::OversamplingQuality prepare(bool nonRealtime) noexcept;
	// releaseResources: requests wait for the next prepare.
	void release() noexcept { prepared.store(false); }

	// Audio thread, start of a block: a requested quality that differs from the active one, now active. The request
	// is claimed before it is read, so a change made meanwhile stays pending for the next block.
	[[nodiscard]] std::optional<dsp::OversamplingQuality> takeRequest(bool nonRealtime) noexcept;

	[[nodiscard]] dsp::OversamplingQuality active() const noexcept { return activeQuality.load(); }
	[[nodiscard]] bool pending() const noexcept { return requestPending.load(); }

private:
	void parameterChanged(const juce::String& identifier, float newValue) override;
	[[nodiscard]] dsp::OversamplingQuality requested(bool nonRealtime) const noexcept;

	juce::AudioProcessorValueTreeState& state;
	const char* trackingId;
	const char* offlineId;
	std::atomic<float>* trackingParameter;
	std::atomic<float>* offlineParameter;
	std::atomic<float> requestedTracking {};
	std::atomic<float> requestedOffline {};
	std::atomic<bool> requestPending {};
	std::atomic<bool> prepared {};
	std::atomic<dsp::OversamplingQuality> activeQuality {};
	static_assert(std::atomic<dsp::OversamplingQuality>::is_always_lock_free);
};
}
