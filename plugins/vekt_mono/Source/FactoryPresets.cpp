#include "FactoryPresets.h"

#include <MonoBinaryData.h>
#include <VektMonoFactoryPresetManifest.h>

namespace vekt::mono
{
juce::Result addFactoryPresets(presets::PresetCatalog& catalog)
{
	return presets::addEmbeddedFactoryPresets(catalog, factory_presets::resources,
		std::size(factory_presets::resources), VektMonoFactoryData::getNamedResource);
}
}
