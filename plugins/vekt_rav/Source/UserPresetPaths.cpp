#include "UserPresetPaths.h"
#include <vekt/presets/PresetPaths.h>

namespace vekt::rav
{
juce::File UserPresetPaths::desktop()
{
	return presets::PresetPaths::desktop("Vekt Rav");
}

juce::Result UserPresetPaths::auv3AppGroup(
	const juce::String& appGroupIdentifier, juce::File& destination)
{
	return presets::PresetPaths::appGroup(appGroupIdentifier, "Vekt Rav", destination);
}

juce::File UserPresetPaths::insideContainer(const juce::File& container)
{
	return presets::PresetPaths::insideContainer(container, "Vekt Rav");
}
}
