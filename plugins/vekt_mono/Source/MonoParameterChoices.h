#pragma once

#include "Lfo.h"
#include "MonoChoiceTypes.h"

#include <vekt/plugin_support/ChoiceTable.h>

namespace vekt::mono
{
// Mono's choice parameters: each table lists its values in choice order with the names the parameter shows. The
// layout builds the parameters from these tables, and the processor and editor decode them with the same tables.
// Projects store the choice index, so a table only ever grows at its end. (LFO divisions keep their own table in Lfo.h.)
inline constexpr plugin_support::ChoiceTable oscillatorRanges { // octaves from 8'
	plugin_support::Choice { -1, "16'" }, plugin_support::Choice { 0, "8'" }, plugin_support::Choice { 1, "4'" },
	plugin_support::Choice { 2, "2'" }, plugin_support::Choice { 3, "1'" }
};
inline constexpr plugin_support::ChoiceTable voiceCounts { plugin_support::Choice { 2, "2" },
	plugin_support::Choice { 4, "4" }, plugin_support::Choice { 8, "8" }, plugin_support::Choice { 12, "12" },
	plugin_support::Choice { 16, "16" } };
inline constexpr plugin_support::ChoiceTable performanceModes {
	plugin_support::Choice { PerformanceMode::poly, "Poly" }, plugin_support::Choice { PerformanceMode::mono, "Mono" },
	plugin_support::Choice { PerformanceMode::monoLegato, "Mono Legato" }
};
inline constexpr plugin_support::ChoiceTable multicoreChoices { plugin_support::Choice { false, "Off" },
	plugin_support::Choice { true, "On" } };
inline constexpr plugin_support::ChoiceTable unisonCounts { plugin_support::Choice { 1, "1x" },
	plugin_support::Choice { 2, "2x" }, plugin_support::Choice { 4, "4x" } };
inline constexpr plugin_support::ChoiceTable glideModes { plugin_support::Choice { GlideMode::off, "Off" },
	plugin_support::Choice { GlideMode::always, "Always" }, plugin_support::Choice { GlideMode::legato, "Legato" } };
inline constexpr plugin_support::ChoiceTable noiseTypes { plugin_support::Choice { NoiseType::off, "Off" },
	plugin_support::Choice { NoiseType::white, "White" }, plugin_support::Choice { NoiseType::pink, "Pink" } };
inline constexpr plugin_support::ChoiceTable notePriorities { plugin_support::Choice { NotePriority::last, "Last" },
	plugin_support::Choice { NotePriority::low, "Low" } };
inline constexpr plugin_support::ChoiceTable vibratoShapes { plugin_support::Choice { LfoShape::sine, "Sine" },
	plugin_support::Choice { LfoShape::triangle, "Triangle" } };
inline constexpr plugin_support::ChoiceTable lfoShapes { plugin_support::Choice { LfoShape::sine, "Sine" },
	plugin_support::Choice { LfoShape::triangle, "Triangle" }, plugin_support::Choice { LfoShape::sawUp, "Saw Up" },
	plugin_support::Choice { LfoShape::sawDown, "Saw Down" }, plugin_support::Choice { LfoShape::square, "Square" },
	plugin_support::Choice { LfoShape::smoothRandom, "Smooth Random" } };
inline constexpr plugin_support::ChoiceTable lfoPolarities { plugin_support::Choice { LfoPolarity::bipolar, "Bipolar" },
	plugin_support::Choice { LfoPolarity::unipolar, "Unipolar" } };
inline constexpr plugin_support::ChoiceTable lfoModes { plugin_support::Choice { LfoMode::free, "Free" },
	plugin_support::Choice { LfoMode::retrigger, "Retrigger" },
	plugin_support::Choice { LfoMode::oneShot, "One Shot" } };
inline constexpr plugin_support::ChoiceTable filterTypes { plugin_support::Choice { FilterType::ladder, "Ladder" },
	plugin_support::Choice { FilterType::svf, "SVF" }, plugin_support::Choice { FilterType::korg35, "K35" } };
}
