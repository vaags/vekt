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
	int schemaVersion { 1 };
	juce::String productIdentifier;
	juce::String name;
	std::vector<ParameterValue> parameters;
	juce::NamedValueSet metadata;
};
}
