#include <vekt/glimmer/Parameters.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

TEST_CASE("Glimmer sound parameters exclude host bypass and quality", "[glimmer][parameters]")
{
	using namespace vekt::glimmer::parameters;

	REQUIRE(presetProductIdentifier == "com.vekt.glimmer");
	REQUIRE(soundParameterIds.size() == 21);
	REQUIRE(std::find(soundParameterIds.begin(), soundParameterIds.end(), mix)
		!= soundParameterIds.end());
	REQUIRE(std::find(soundParameterIds.begin(), soundParameterIds.end(), bypass)
		== soundParameterIds.end());
	REQUIRE(std::find(soundParameterIds.begin(), soundParameterIds.end(), trackingOversampling)
		== soundParameterIds.end());
	REQUIRE(std::find(soundParameterIds.begin(), soundParameterIds.end(), offlineOversampling)
		== soundParameterIds.end());
}

TEST_CASE("Glimmer quality mappings match the shared quality choices", "[glimmer][parameters]")
{
	using namespace vekt::glimmer::parameters;

	REQUIRE(trackingQualityFrom(2.0f).factor == vekt::dsp::OversamplingFactor::x4);
	REQUIRE(trackingQualityFrom(2.0f).filter == vekt::dsp::OversamplingFilter::polyphaseIIR);
	REQUIRE(offlineQualityFrom(4.0f).factor == vekt::dsp::OversamplingFactor::x16);
	REQUIRE(offlineQualityFrom(4.0f).filter == vekt::dsp::OversamplingFilter::polyphaseFIR);
}
