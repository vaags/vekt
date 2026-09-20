#pragma once

#include <vekt/presets/PresetCatalog.h>

#include <cstddef>

namespace vekt::presets
{
struct EmbeddedFactoryPreset final
{
	const char* resourceName;
	const char* folder;
};

using EmbeddedPresetLookup = const char* (*)(const char* resourceName, int& sizeInBytes);

[[nodiscard]] juce::Result addEmbeddedFactoryPresets(PresetCatalog& catalog,
	const EmbeddedFactoryPreset* resources, std::size_t resourceCount, EmbeddedPresetLookup lookup);
}
