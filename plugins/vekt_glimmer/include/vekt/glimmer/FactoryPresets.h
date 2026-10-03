#pragma once

#include <vekt/presets/PresetCatalog.h>

namespace vekt::glimmer
{
[[nodiscard]] juce::Result addFactoryPresets(presets::PresetCatalog& catalog);
}
