#include "FactoryPresets.h"

#include <BinaryData.h>

#include <array>

namespace vekt::rav
{
juce::Result addFactoryPresets(presets::PresetCatalog& catalog)
{
	const std::array documents {
		juce::String::fromUTF8(VektRavFactoryData::Clean_Heat_vektpreset,
			VektRavFactoryData::Clean_Heat_vektpresetSize),
		juce::String::fromUTF8(VektRavFactoryData::Warm_Push_vektpreset,
			VektRavFactoryData::Warm_Push_vektpresetSize),
		juce::String::fromUTF8(VektRavFactoryData::Parallel_Grit_vektpreset,
			VektRavFactoryData::Parallel_Grit_vektpresetSize)
	};

	for (const auto& document : documents)
		if (const auto result = catalog.addFactoryPreset(document); result.failed())
			return result;

	return juce::Result::ok();
}
}
