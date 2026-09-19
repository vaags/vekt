#include <vekt/mono/Parameters.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

TEST_CASE("Mono sound parameters exclude resource configuration", "[mono][parameters]")
{
	using namespace vekt::mono::parameters;
	REQUIRE(presetProductIdentifier == "com.vekt.mono");
	REQUIRE(std::find(soundParameterIds.begin(), soundParameterIds.end(), filterCutoff) != soundParameterIds.end());
	REQUIRE(std::find(soundParameterIds.begin(), soundParameterIds.end(), voiceCount) == soundParameterIds.end());
	REQUIRE(std::find(soundParameterIds.begin(), soundParameterIds.end(), quality) == soundParameterIds.end());
}