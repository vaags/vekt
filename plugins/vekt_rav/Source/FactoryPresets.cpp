#include "FactoryPresets.h"

#include <BinaryData.h>
#include <VektRavFactoryPresetManifest.h>

namespace vekt::rav
{
juce::Result addFactoryPresets(presets::PresetCatalog& catalog)
{
	return presets::addEmbeddedFactoryPresets(catalog, factory_presets::resources,
		std::size(factory_presets::resources), VektRavFactoryData::getNamedResource);
}
}
