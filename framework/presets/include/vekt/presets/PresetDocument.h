#pragma once

#include <vekt/presets/Preset.h>
#include <cmath>

namespace vekt::presets
{
// Global envelope validation has no dependency on a plugin or APVTS.
struct PresetDocument final
{
	inline static constexpr auto format = "vekt.preset";
	inline static constexpr int version = 2;

	[[nodiscard]] static juce::Result validate(const Preset& preset)
	{
		if (preset.schemaVersion != version)
			return juce::Result::fail("Unsupported preset format version");
		if (preset.soundSchemaVersion < 1 || preset.identifier.trim().isEmpty())
			return juce::Result::fail("Invalid preset identity or sound version");
		if (preset.productIdentifier.trim().isEmpty() || preset.name.trim().isEmpty())
			return juce::Result::fail("Preset product and name are required");
		if (preset.parameters.empty() && preset.soundState.size() == 0)
			return juce::Result::fail("Preset sound is missing");
		juce::StringArray identifiers;
		for (const auto& parameter : preset.parameters)
		{
			if (parameter.identifier.isEmpty() || !std::isfinite(parameter.value)
				|| identifiers.contains(parameter.identifier))
				return juce::Result::fail("Invalid or duplicate sound parameter");
			identifiers.add(parameter.identifier);
		}
		for (const auto& tag : preset.tags)
			if (tag.trim().isEmpty() || tag.length() > 80)
				return juce::Result::fail("Tags must contain between 1 and 80 characters");
		return juce::Result::ok();
	}
};
}