#include "UserPresetPaths.h"

namespace vekt::saturator
{
namespace
{
[[nodiscard]] juce::File appendProductPath(const juce::File& root)
{
	return root.getChildFile("Library")
		.getChildFile("Audio")
		.getChildFile("Presets")
		.getChildFile("Vekt")
		.getChildFile("Vekt Saturator");
}
}

juce::File UserPresetPaths::desktop()
{
	return appendProductPath(
		juce::File::getSpecialLocation(juce::File::userHomeDirectory));
}

juce::Result UserPresetPaths::auv3AppGroup(
	const juce::String& appGroupIdentifier, juce::File& destination)
{
	if (appGroupIdentifier.trim().isEmpty())
		return juce::Result::fail("AUv3 user presets require an app-group identifier");

	const auto container = juce::File::getContainerForSecurityApplicationGroupIdentifier(
		appGroupIdentifier.trim());
	if (container == juce::File {})
		return juce::Result::fail("AUv3 app-group container is unavailable");

	destination = insideContainer(container);
	return juce::Result::ok();
}

juce::File UserPresetPaths::insideContainer(const juce::File& container)
{
	return appendProductPath(container);
}
}
