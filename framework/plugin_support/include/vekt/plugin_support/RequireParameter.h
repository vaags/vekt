#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace vekt::plugin_support
{
// A parameter's value, resolved by ID once (at construction) so the audio thread never looks one up by name.
[[nodiscard]] inline std::atomic<float>* requireParameter(juce::AudioProcessorValueTreeState& state, const char* identifier)
{
	auto* parameter = state.getRawParameterValue(identifier);
	jassert(parameter != nullptr);
	return parameter;
}
}
