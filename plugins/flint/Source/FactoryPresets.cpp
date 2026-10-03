#include <vekt/flint/FactoryPresets.h>

#include <FlintBinaryData.h>
#include <FlintFactoryPresetManifest.h>

namespace vekt::flint
{
juce::Result addFactoryPresets(presets::PresetCatalog& catalog)
{
	return presets::addEmbeddedFactoryPresets(
	    catalog, factory_presets::resources, std::size(factory_presets::resources), FlintFactoryData::getNamedResource);
}
}
