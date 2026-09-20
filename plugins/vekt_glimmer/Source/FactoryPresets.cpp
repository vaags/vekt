#include "FactoryPresets.h"

#include <GlimmerBinaryData.h>
#include <VektGlimmerFactoryPresetManifest.h>

namespace vekt::glimmer
{
juce::Result addFactoryPresets(presets::PresetCatalog& catalog)
{
	return presets::addEmbeddedFactoryPresets(catalog, factory_presets::resources,
		std::size(factory_presets::resources), VektGlimmerFactoryData::getNamedResource);
}
}
