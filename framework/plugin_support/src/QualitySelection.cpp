#include <vekt/plugin_support/QualitySelection.h>

#include <vekt/plugin_support/RequireParameter.h>

namespace vekt::plugin_support
{
std::unique_ptr<juce::AudioParameterChoice> QualitySelection::makeTrackingParameter(
	const char* identifier, int versionHint, int defaultIndex)
{
	return std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { identifier, versionHint },
		"Tracking Oversampling", dsp::trackingQualityChoices(), defaultIndex,
		juce::AudioParameterChoiceAttributes {}.withAutomatable(false));
}

std::unique_ptr<juce::AudioParameterChoice> QualitySelection::makeOfflineParameter(
	const char* identifier, int versionHint, int defaultIndex)
{
	return std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { identifier, versionHint },
		"Offline Oversampling", dsp::offlineQualityChoices(), defaultIndex,
		juce::AudioParameterChoiceAttributes {}.withAutomatable(false));
}

QualitySelection::QualitySelection(juce::AudioProcessorValueTreeState& parameters, const char* trackingIdentifier,
	const char* offlineIdentifier)
	: state(parameters),
	  trackingId(trackingIdentifier),
	  offlineId(offlineIdentifier),
	  trackingParameter(requireParameter(parameters, trackingIdentifier)),
	  offlineParameter(requireParameter(parameters, offlineIdentifier))
{
	requestedTracking.store(trackingParameter->load());
	requestedOffline.store(offlineParameter->load());
	activeQuality.store(requested(false));
	state.addParameterListener(trackingId, this);
	state.addParameterListener(offlineId, this);
}

QualitySelection::~QualitySelection()
{
	state.removeParameterListener(trackingId, this);
	state.removeParameterListener(offlineId, this);
}

dsp::OversamplingQuality QualitySelection::prepare(bool nonRealtime) noexcept
{
	requestPending.store(false);
	requestedTracking.store(trackingParameter->load());
	requestedOffline.store(offlineParameter->load());
	const auto quality = requested(nonRealtime);
	activeQuality.store(quality);
	prepared.store(true);
	return quality;
}

std::optional<dsp::OversamplingQuality> QualitySelection::takeRequest(bool nonRealtime) noexcept
{
	if (!prepared.load() || !requestPending.exchange(false))
		return std::nullopt;
	const auto quality = requested(nonRealtime);
	if (quality == activeQuality.load())
		return std::nullopt;
	activeQuality.store(quality);
	return quality;
}

void QualitySelection::parameterChanged(const juce::String& identifier, float newValue)
{
	if (identifier == trackingId)
		requestedTracking.store(newValue);
	else if (identifier == offlineId)
		requestedOffline.store(newValue);
	else
		return;
	requestPending.store(true);
}

dsp::OversamplingQuality QualitySelection::requested(bool nonRealtime) const noexcept
{
	return nonRealtime ? dsp::offlineQualityFrom(requestedOffline.load()) : dsp::trackingQualityFrom(requestedTracking.load());
}
}
