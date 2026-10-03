#pragma once

#include "RotaryEngine.h"
#include "RotorMotion.h"

#include <vekt/plugin_support/ChoiceTable.h>

namespace vekt::glimmer
{
// Glimmer's choice parameters: each table lists its values in choice order with the names the parameter shows. The
// layout builds the parameters from these tables and the processor and editor decode with them. Projects store the
// choice index, so a table only ever grows at its end.
inline constexpr plugin_support::ChoiceTable speedModes { plugin_support::Choice { RotarySpeedMode::slow, "Slow" },
	plugin_support::Choice { RotarySpeedMode::fast, "Fast" },
	plugin_support::Choice { RotarySpeedMode::autoMode, "Auto" } };
inline constexpr plugin_support::ChoiceTable cabinetModels { plugin_support::Choice {
	                                                             CabinetModel::classic, "Classic" },
	plugin_support::Choice { CabinetModel::drum, "Drum" }, plugin_support::Choice { CabinetModel::wide, "Wide" } };
}
