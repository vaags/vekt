#include <vekt/mono/FactoryPresets.h>

#include <MonoBinaryData.h>
#include <KobberFactoryPresetManifest.h>

namespace vekt::mono
{
juce::Result addFactoryPresets(presets::PresetCatalog& catalog)
{
	return presets::addEmbeddedFactoryPresets(catalog, factory_presets::resources,
		std::size(factory_presets::resources), KobberFactoryData::getNamedResource);
}
}
