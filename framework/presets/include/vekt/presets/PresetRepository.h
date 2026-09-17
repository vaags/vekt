#pragma once

#include <vekt/presets/Preset.h>

namespace vekt::presets
{
class PresetRepository
{
public:
	virtual ~PresetRepository() = default;

	[[nodiscard]] virtual juce::StringArray list() const = 0;
	[[nodiscard]] virtual juce::Result load(
		const juce::String& name, Preset& destination) const = 0;
	[[nodiscard]] virtual juce::Result save(const Preset& preset) = 0;
	[[nodiscard]] virtual juce::Result remove(const juce::String& name) = 0;
};
}
