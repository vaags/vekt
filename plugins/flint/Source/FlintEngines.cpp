#include "FlintEngines.h"

#include "KickClassicAnalog.h"
#include "MalletBar.h"

namespace vekt::flint
{
FlintEngines makeFlintEngines()
{
	FlintEngines engines;
	engines[indexOf(ModelId::kickClassicAnalog)] = std::make_unique<KickClassicAnalog>();
	engines[indexOf(ModelId::malletBar)] = std::make_unique<MalletBar>();
	return engines;
}
}
