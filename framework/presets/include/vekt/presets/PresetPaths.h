#pragma once

#include <juce_core/juce_core.h>

namespace vekt::presets
{
struct PresetPaths final
{
	[[nodiscard]] static juce::File insideContainer(const juce::File& container, const juce::String& storageName)
	{ return container.getChildFile("Library/Audio/Presets/Vekt").getChildFile(juce::File::createLegalFileName(storageName)); }
	[[nodiscard]] static juce::File desktop(const juce::String& storageName)
	{ return insideContainer(juce::File::getSpecialLocation(juce::File::userHomeDirectory), storageName); }
};
}
