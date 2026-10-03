#include <vekt/rav/FactoryPresets.h>

#include <BinaryData.h>
#include <RavFactoryPresetManifest.h>

namespace vekt::rav
{
juce::Result addFactoryPresets(presets::PresetCatalog& catalog)
{
	return presets::addEmbeddedFactoryPresets(catalog, factory_presets::resources,
		std::size(factory_presets::resources), RavFactoryData::getNamedResource);
}
}
