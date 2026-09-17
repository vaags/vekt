#pragma once

#include <juce_core/juce_core.h>

namespace vekt::rav
{
class UserPresetPaths final
{
public:
	[[nodiscard]] static juce::File desktop();
	[[nodiscard]] static juce::Result auv3AppGroup(
		const juce::String& appGroupIdentifier, juce::File& destination);
	[[nodiscard]] static juce::File insideContainer(const juce::File& container);
};
}
