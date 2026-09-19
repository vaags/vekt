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
	[[nodiscard]] static juce::Result appGroup(const juce::String& group, const juce::String& storageName, juce::File& destination)
	{
		if (group.trim().isEmpty()) return juce::Result::fail("AUv3 user presets require an app-group identifier");
		const auto container = juce::File::getContainerForSecurityApplicationGroupIdentifier(group);
		if (container == juce::File {}) return juce::Result::fail("AUv3 app-group container is unavailable");
		destination = insideContainer(container, storageName);
		return juce::Result::ok();
	}
};
}