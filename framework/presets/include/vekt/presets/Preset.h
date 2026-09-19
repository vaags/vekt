#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace vekt::presets
{
struct ParameterValue final
{
	juce::String identifier;
	float value {};
};

struct Preset final
{
	int schemaVersion { 2 };
	int soundSchemaVersion { 1 };
	juce::String identifier { juce::Uuid().toString() };
	juce::String productIdentifier;
	juce::String name;
	juce::StringArray tags;
	// Repository-relative folder, deliberately excluded from portable documents.
	juce::String folder;
	std::vector<ParameterValue> parameters;
	juce::NamedValueSet metadata;
	juce::NamedValueSet soundState;
};

[[nodiscard]] inline juce::StringArray normaliseTags(const juce::StringArray& values)
{
	juce::StringArray result;
	for (const auto& value : values)
		if (const auto tag = value.trim(); tag.isNotEmpty())
			result.addIfNotAlreadyThere(tag, true);
	result.sortNatural();
	return result;
}
}
