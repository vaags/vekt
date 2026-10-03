#pragma once

#include "RavModeStage.h"

#include <vekt/plugin_support/ChoiceTable.h>

namespace vekt::rav
{
// The Mode parameter's values in choice order with the names it shows: the layout builds the parameter from this table
// and the processor decodes with it. Projects store the choice index, so it only ever grows at its end.
inline constexpr plugin_support::ChoiceTable ravModes { plugin_support::Choice { RavMode::saturation, "Saturation" },
	plugin_support::Choice { RavMode::overdrive, "Overdrive" },
	plugin_support::Choice { RavMode::distortion, "Distortion" },
	plugin_support::Choice { RavMode::circuitFuzz, "Circuit Fuzz" },
	plugin_support::Choice { RavMode::gatedFuzz, "Gated Fuzz" } };
static_assert(ravModes.size() == ravModeCount);
}
