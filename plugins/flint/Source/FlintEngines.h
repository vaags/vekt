#pragma once

#include "FlintEngineHost.h"

namespace vekt::flint
{
// Flint's implemented engines, one per model slot; the rest stay empty and play silence.
[[nodiscard]] FlintEngines makeFlintEngines();
}
