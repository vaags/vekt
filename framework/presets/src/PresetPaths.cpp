#include <vekt/presets/PresetPaths.h>

namespace vekt::presets
{
juce::File PresetPaths::insideContainer(const juce::File& container, const juce::String& storageName)
{
	return container.getChildFile("Library/Audio/Presets")
	    .getChildFile(VEKT_MAKER_NAME)
	    .getChildFile(juce::File::createLegalFileName(storageName));
}

juce::File PresetPaths::desktop(const juce::String& storageName)
{
	return insideContainer(juce::File::getSpecialLocation(juce::File::userHomeDirectory), storageName);
}
}
