#pragma once

#include <vekt/presets/PresetCatalog.h>

namespace vekt::flint
{
[[nodiscard]] juce::Result addFactoryPresets(presets::PresetCatalog& catalog);
}
