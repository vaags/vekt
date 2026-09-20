#include <vekt/presets/EmbeddedFactoryPresets.h>

namespace vekt::presets
{
juce::Result addEmbeddedFactoryPresets(PresetCatalog& catalog,
	const EmbeddedFactoryPreset* resources, std::size_t resourceCount, EmbeddedPresetLookup lookup)
{
	if (resources == nullptr || lookup == nullptr)
		return juce::Result::fail("Embedded factory preset resources are unavailable");

	for (std::size_t index {}; index < resourceCount; ++index)
	{
		const auto& resource = resources[index];
		int size {};
		const auto* data = lookup(resource.resourceName, size);
		if (data == nullptr || size <= 0)
			return juce::Result::fail("Embedded factory preset resource is unavailable: " + juce::String(resource.resourceName));
		if (const auto result = catalog.addFactoryPreset(
			juce::String::fromUTF8(data, size), resource.folder); result.failed())
			return juce::Result::fail("Could not register embedded factory preset "
				+ juce::String(resource.resourceName) + ": " + result.getErrorMessage());
	}

	return juce::Result::ok();
}
}
