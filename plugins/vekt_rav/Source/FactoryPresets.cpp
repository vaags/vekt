#include "FactoryPresets.h"

#include <BinaryData.h>

namespace vekt::rav
{
juce::Result addFactoryPresets(presets::PresetCatalog& catalog)
{
	struct FactoryResource final { const char* data; int size; const char* folder; };
	const FactoryResource documents[] {
		{ VektRavFactoryData::Clean_Heat_vektpreset, VektRavFactoryData::Clean_Heat_vektpresetSize, "" },
		{ VektRavFactoryData::Warm_Push_vektpreset, VektRavFactoryData::Warm_Push_vektpresetSize, "" },
		{ VektRavFactoryData::Parallel_Grit_vektpreset, VektRavFactoryData::Parallel_Grit_vektpresetSize, "" },
		{ VektRavFactoryData::Deep_Foundation_vektpreset, VektRavFactoryData::Deep_Foundation_vektpresetSize, "Bass" },
		{ VektRavFactoryData::Grind_Line_vektpreset, VektRavFactoryData::Grind_Line_vektpresetSize, "Bass" },
		{ VektRavFactoryData::Parallel_Punch_vektpreset, VektRavFactoryData::Parallel_Punch_vektpresetSize, "Bass" },
		{ VektRavFactoryData::Edge_Breakup_vektpreset, VektRavFactoryData::Edge_Breakup_vektpresetSize, "Guitar" },
		{ VektRavFactoryData::Lead_Bite_vektpreset, VektRavFactoryData::Lead_Bite_vektpresetSize, "Guitar" },
		{ VektRavFactoryData::Velvet_Drive_vektpreset, VektRavFactoryData::Velvet_Drive_vektpresetSize, "Guitar" },
		{ VektRavFactoryData::Analog_Glow_vektpreset, VektRavFactoryData::Analog_Glow_vektpresetSize, "Keys" },
		{ VektRavFactoryData::Crushed_Chords_vektpreset, VektRavFactoryData::Crushed_Chords_vektpresetSize, "Keys" },
		{ VektRavFactoryData::Velvet_Electric_vektpreset, VektRavFactoryData::Velvet_Electric_vektpresetSize, "Keys" },
		{ VektRavFactoryData::Drum_Bus_Glue_vektpreset, VektRavFactoryData::Drum_Bus_Glue_vektpresetSize, "Drums" },
		{ VektRavFactoryData::Snare_Crack_vektpreset, VektRavFactoryData::Snare_Crack_vektpresetSize, "Drums" },
		{ VektRavFactoryData::Transient_Smash_vektpreset, VektRavFactoryData::Transient_Smash_vektpresetSize, "Drums" },
		{ VektRavFactoryData::Low_End_Focus_vektpreset, VektRavFactoryData::Low_End_Focus_vektpresetSize, "Mastering" },
		{ VektRavFactoryData::Parallel_Sheen_vektpreset, VektRavFactoryData::Parallel_Sheen_vektpresetSize, "Mastering" },
		{ VektRavFactoryData::Transparent_Polish_vektpreset, VektRavFactoryData::Transparent_Polish_vektpresetSize, "Mastering" },
		{ VektRavFactoryData::Circuit_Grind_vektpreset, VektRavFactoryData::Circuit_Grind_vektpresetSize, "Bass" },
		{ VektRavFactoryData::Circuit_Sustain_vektpreset, VektRavFactoryData::Circuit_Sustain_vektpresetSize, "Guitar" },
		{ VektRavFactoryData::Circuit_Bloom_vektpreset, VektRavFactoryData::Circuit_Bloom_vektpresetSize, "Keys" },
		{ VektRavFactoryData::Circuit_Parallel_vektpreset, VektRavFactoryData::Circuit_Parallel_vektpresetSize, "Drums" }
	};

	for (const auto& document : documents)
		if (const auto result = catalog.addFactoryPreset(
			juce::String::fromUTF8(document.data, document.size), document.folder); result.failed())
			return result;

	return juce::Result::ok();
}
}
