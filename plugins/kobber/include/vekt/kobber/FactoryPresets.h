#pragma once

#include <vekt/presets/PresetCatalog.h>

namespace vekt::kobber
{
[[nodiscard]] juce::Result addFactoryPresets(presets::PresetCatalog& catalog);
}