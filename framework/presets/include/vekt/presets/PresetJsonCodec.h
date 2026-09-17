#pragma once

#include <vekt/presets/Preset.h>

namespace vekt::presets
{
class PresetJsonCodec final
{
public:
	[[nodiscard]] static juce::Result encode(const Preset& preset, juce::String& destination);
	[[nodiscard]] static juce::Result decode(const juce::String& json, Preset& destination);
};
}