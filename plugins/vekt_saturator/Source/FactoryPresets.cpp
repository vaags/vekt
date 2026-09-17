#include "FactoryPresets.h"

#include <BinaryData.h>

#include <array>

namespace vekt::saturator
{
juce::Result addFactoryPresets(presets::PresetCatalog& catalog)
{
	const std::array documents {
		juce::String::fromUTF8(VektSaturatorFactoryData::Clean_Heat_vektpreset,
			VektSaturatorFactoryData::Clean_Heat_vektpresetSize),
		juce::String::fromUTF8(VektSaturatorFactoryData::Warm_Push_vektpreset,
			VektSaturatorFactoryData::Warm_Push_vektpresetSize),
		juce::String::fromUTF8(VektSaturatorFactoryData::Parallel_Grit_vektpreset,
			VektSaturatorFactoryData::Parallel_Grit_vektpresetSize)
	};

	for (const auto& document : documents)
		if (const auto result = catalog.addFactoryPreset(document); result.failed())
			return result;

	return juce::Result::ok();
}
}
