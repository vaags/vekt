#pragma once

#include <vekt/presets/PresetCatalog.h>

namespace vekt::rav
{
[[nodiscard]] juce::Result addFactoryPresets(presets::PresetCatalog& catalog);
}
