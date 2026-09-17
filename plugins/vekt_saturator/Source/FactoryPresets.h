#pragma once

#include <vekt/presets/PresetCatalog.h>

namespace vekt::saturator
{
[[nodiscard]] juce::Result addFactoryPresets(presets::PresetCatalog& catalog);
}
