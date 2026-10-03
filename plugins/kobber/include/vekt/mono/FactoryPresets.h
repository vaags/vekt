#pragma once

#include <vekt/presets/PresetCatalog.h>

namespace vekt::mono
{
[[nodiscard]] juce::Result addFactoryPresets(presets::PresetCatalog& catalog);
}