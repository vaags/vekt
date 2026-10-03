#pragma once

#include <juce_core/juce_core.h>

namespace vekt::presets
{
// User presets live under Library/Audio/Presets/<maker>/<product>; the maker comes from the build (VEKT_MAKER_NAME).
struct PresetPaths final
{
	[[nodiscard]] static juce::File insideContainer(const juce::File& container, const juce::String& storageName);
	[[nodiscard]] static juce::File desktop(const juce::String& storageName);
};
}
