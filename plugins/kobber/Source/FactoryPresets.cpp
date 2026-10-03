#include <vekt/kobber/FactoryPresets.h>

#include <KobberBinaryData.h>
#include <KobberFactoryPresetManifest.h>

namespace vekt::kobber
{
juce::Result addFactoryPresets(presets::PresetCatalog& catalog)
{
	return presets::addEmbeddedFactoryPresets(catalog, factory_presets::resources,
		std::size(factory_presets::resources), KobberFactoryData::getNamedResource);
}
}
