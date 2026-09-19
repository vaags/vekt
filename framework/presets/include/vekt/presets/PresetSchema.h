#pragma once

#include <vekt/presets/Preset.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include <span>

namespace vekt::presets
{
class PresetSchema final
{
public:
	[[nodiscard]] static Preset create(
		const juce::String& productIdentifier,
		const juce::String& name,
		const juce::AudioProcessorValueTreeState& parameters,
		std::span<const char* const> soundParameterIds,
		const juce::NamedValueSet& metadata = {});

	[[nodiscard]] static juce::Result validate(
		const Preset& preset,
		const juce::String& expectedProductIdentifier,
		const juce::AudioProcessorValueTreeState& parameters,
		std::span<const char* const> soundParameterIds);
	[[nodiscard]] static juce::Result validateEnvelope(const Preset& preset);
	[[nodiscard]] static bool matches(
		const Preset& preset,
		const juce::String& expectedProductIdentifier,
		const juce::AudioProcessorValueTreeState& parameters,
		std::span<const char* const> soundParameterIds);

	[[nodiscard]] static juce::Result apply(
		const Preset& preset,
		const juce::String& expectedProductIdentifier,
		juce::AudioProcessorValueTreeState& parameters,
		std::span<const char* const> soundParameterIds,
		juce::UndoManager* undoManager = nullptr);

	inline static constexpr auto currentSchemaVersion = 2;
};
}
