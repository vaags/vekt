#pragma once

#include <vekt/presets/Preset.h>

namespace vekt::presets
{
enum class PresetSaveMode
{
	createOnly,
	replaceExisting
};

class PresetRepository
{
public:
	virtual ~PresetRepository() = default;

	[[nodiscard]] virtual juce::StringArray list() const = 0;
	[[nodiscard]] virtual juce::Result load(
		const juce::String& name, Preset& destination) const = 0;
	[[nodiscard]] virtual juce::Result save(
		const Preset& preset, PresetSaveMode mode = PresetSaveMode::createOnly) = 0;
	[[nodiscard]] virtual juce::Result remove(const juce::String& name) = 0;
	[[nodiscard]] virtual juce::StringArray folders() const { return {}; }
	[[nodiscard]] virtual juce::Result createFolder(const juce::String&)
	{ return juce::Result::fail("Folders are not supported by this repository"); }
	[[nodiscard]] virtual juce::Result move(const juce::String&, const juce::String&)
	{ return juce::Result::fail("Moving is not supported by this repository"); }
};
}
