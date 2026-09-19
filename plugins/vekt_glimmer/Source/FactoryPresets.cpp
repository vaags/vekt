#include "FactoryPresets.h"

#include <GlimmerBinaryData.h>

namespace vekt::glimmer
{
juce::Result addFactoryPresets(presets::PresetCatalog& catalog)
{
	struct FactoryResource final { const char* data; int size; const char* folder; };
	const FactoryResource documents[] {
		{ VektGlimmerFactoryData::Classic_Chorale_vektpreset, VektGlimmerFactoryData::Classic_Chorale_vektpresetSize, "Classic" },
		{ VektGlimmerFactoryData::Gospel_Spin_vektpreset, VektGlimmerFactoryData::Gospel_Spin_vektpresetSize, "Classic" },
		{ VektGlimmerFactoryData::Baffle_Drive_vektpreset, VektGlimmerFactoryData::Baffle_Drive_vektpresetSize, "Drum" },
		{ VektGlimmerFactoryData::Dynamic_Drum_vektpreset, VektGlimmerFactoryData::Dynamic_Drum_vektpresetSize, "Drum" },
		{ VektGlimmerFactoryData::Glass_Motion_vektpreset, VektGlimmerFactoryData::Glass_Motion_vektpresetSize, "Wide" },
		{ VektGlimmerFactoryData::Slow_Panorama_vektpreset, VektGlimmerFactoryData::Slow_Panorama_vektpresetSize, "Wide" }
	};
	for (const auto& document : documents)
		if (const auto result = catalog.addFactoryPreset(
			juce::String::fromUTF8(document.data, document.size), document.folder); result.failed())
			return result;
	return juce::Result::ok();
}
}