#include "FactoryPresets.h"

#include <MonoBinaryData.h>

namespace vekt::mono
{
juce::Result addFactoryPresets(presets::PresetCatalog& catalog)
{
	struct FactoryResource final { const char* data; int size; const char* folder; };
	const FactoryResource documents[] {
		{ VektMonoFactoryData::Aurora_Lead_vektpreset, VektMonoFactoryData::Aurora_Lead_vektpresetSize, "Lead" },
		{ VektMonoFactoryData::Circuit_Lead_vektpreset, VektMonoFactoryData::Circuit_Lead_vektpresetSize, "Lead" },
		{ VektMonoFactoryData::Glass_Lead_vektpreset, VektMonoFactoryData::Glass_Lead_vektpresetSize, "Lead" },
		{ VektMonoFactoryData::Voyager_Lead_vektpreset, VektMonoFactoryData::Voyager_Lead_vektpresetSize, "Lead" },
		{ VektMonoFactoryData::Foundation_Bass_vektpreset, VektMonoFactoryData::Foundation_Bass_vektpresetSize, "Bass" },
		{ VektMonoFactoryData::Rubber_Bass_vektpreset, VektMonoFactoryData::Rubber_Bass_vektpresetSize, "Bass" },
		{ VektMonoFactoryData::Steel_Bass_vektpreset, VektMonoFactoryData::Steel_Bass_vektpresetSize, "Bass" },
		{ VektMonoFactoryData::Subway_Bass_vektpreset, VektMonoFactoryData::Subway_Bass_vektpresetSize, "Bass" },
		{ VektMonoFactoryData::Amber_Pad_vektpreset, VektMonoFactoryData::Amber_Pad_vektpresetSize, "Pad" },
		{ VektMonoFactoryData::Horizon_Pad_vektpreset, VektMonoFactoryData::Horizon_Pad_vektpresetSize, "Pad" },
		{ VektMonoFactoryData::Nocturne_Pad_vektpreset, VektMonoFactoryData::Nocturne_Pad_vektpresetSize, "Pad" },
		{ VektMonoFactoryData::Vapor_Pad_vektpreset, VektMonoFactoryData::Vapor_Pad_vektpresetSize, "Pad" },
		{ VektMonoFactoryData::Copper_Pluck_vektpreset, VektMonoFactoryData::Copper_Pluck_vektpresetSize, "Pluck" },
		{ VektMonoFactoryData::Ember_Pluck_vektpreset, VektMonoFactoryData::Ember_Pluck_vektpresetSize, "Pluck" },
		{ VektMonoFactoryData::Needle_Pluck_vektpreset, VektMonoFactoryData::Needle_Pluck_vektpresetSize, "Pluck" },
		{ VektMonoFactoryData::Slate_Pluck_vektpreset, VektMonoFactoryData::Slate_Pluck_vektpresetSize, "Pluck" },
		{ VektMonoFactoryData::Current_Motion_vektpreset, VektMonoFactoryData::Current_Motion_vektpresetSize, "Motion" },
		{ VektMonoFactoryData::Orbit_Motion_vektpreset, VektMonoFactoryData::Orbit_Motion_vektpresetSize, "Motion" },
		{ VektMonoFactoryData::Pulse_Motion_vektpreset, VektMonoFactoryData::Pulse_Motion_vektpresetSize, "Motion" },
		{ VektMonoFactoryData::Tide_Motion_vektpreset, VektMonoFactoryData::Tide_Motion_vektpresetSize, "Motion" },
		{ VektMonoFactoryData::Comet_FX_vektpreset, VektMonoFactoryData::Comet_FX_vektpresetSize, "FX" },
		{ VektMonoFactoryData::Entropy_FX_vektpreset, VektMonoFactoryData::Entropy_FX_vektpresetSize, "FX" },
		{ VektMonoFactoryData::Polaris_FX_vektpreset, VektMonoFactoryData::Polaris_FX_vektpresetSize, "FX" },
		{ VektMonoFactoryData::Transmission_FX_vektpreset, VektMonoFactoryData::Transmission_FX_vektpresetSize, "FX" }
	};
	for (const auto& document : documents)
		if (const auto result = catalog.addFactoryPreset(
			juce::String::fromUTF8(document.data, document.size), document.folder); result.failed())
			return result;
	return juce::Result::ok();
}
}