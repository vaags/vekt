#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <cstdlib>

namespace vekt::plugin_support
{
// A parameter's value, resolved by ID once (at construction) so the audio thread never looks one up by name. An
// identifier the state does not hold is a programming error: it stops in every build instead of leaving a null value
// for the first audio block to dereference.
[[nodiscard]] inline std::atomic<float>& requireParameter(juce::AudioProcessorValueTreeState& state, const char* identifier)
{
	auto* parameter = state.getRawParameterValue(identifier);
	if (parameter == nullptr)
	{
		jassertfalse;
		std::abort();
	}
	return *parameter;
}
}
