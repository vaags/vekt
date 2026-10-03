#include <vekt/mono/PluginProcessor.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>

TEST_CASE("Mono sound parameters exclude resource configuration", "[mono][parameters]")
{
	using namespace vekt::mono::parameters;
	REQUIRE(juce::String(presetProductIdentifier) == "com.vekt.mono");
	REQUIRE(std::find(soundParameterIds.begin(), soundParameterIds.end(), filterCutoff) != soundParameterIds.end());
	REQUIRE(std::find(soundParameterIds.begin(), soundParameterIds.end(), heldKeyReturn) != soundParameterIds.end());
	REQUIRE(std::find(soundParameterIds.begin(), soundParameterIds.end(), filterQCompensation) != soundParameterIds.end());
	REQUIRE(std::find(soundParameterIds.begin(), soundParameterIds.end(), ampRelease) != soundParameterIds.end());
	REQUIRE(std::find(soundParameterIds.begin(), soundParameterIds.end(), filterRelease) != soundParameterIds.end());
	REQUIRE(std::find(soundParameterIds.begin(), soundParameterIds.end(), voiceCount) == soundParameterIds.end());
	REQUIRE(std::find(soundParameterIds.begin(), soundParameterIds.end(), trackingOversampling) == soundParameterIds.end());
	REQUIRE(std::find(soundParameterIds.begin(), soundParameterIds.end(), offlineOversampling) == soundParameterIds.end());
}

TEST_CASE("Mono exposes only independent amp and filter release controls", "[mono][parameters][contour]")
{
	vekt::mono::PluginProcessor processor;
	REQUIRE(processor.getParameters().getParameter("contourCurve") == nullptr);
	REQUIRE(processor.getParameters().getParameter("releasePolicy") == nullptr);
	REQUIRE(processor.getParameters().getParameter(vekt::mono::parameters::ampRelease) != nullptr);
	REQUIRE(processor.getParameters().getParameter(vekt::mono::parameters::filterRelease) != nullptr);
}

TEST_CASE("Mono oscillator tuning exposes musical octave and cents ranges", "[mono][parameters]")
{
	vekt::mono::PluginProcessor processor;
	const auto find = [&processor](const char* identifier)
	{
		return dynamic_cast<juce::RangedAudioParameter*>(processor.getParameters().getParameter(identifier));
	};
	for (const auto* identifier : { vekt::mono::parameters::osc1Octave, vekt::mono::parameters::osc2Octave, vekt::mono::parameters::osc3Octave })
	{
		const auto* parameter = find(identifier);
		REQUIRE(parameter != nullptr);
		REQUIRE(parameter->getNormalisableRange().start == Catch::Approx(-2.0f));
		REQUIRE(parameter->getNormalisableRange().end == Catch::Approx(2.0f));
		REQUIRE(parameter->getNormalisableRange().interval == Catch::Approx(1.0f));
	}
	for (const auto* identifier : { vekt::mono::parameters::osc1Fine, vekt::mono::parameters::osc2Fine, vekt::mono::parameters::osc3Fine })
	{
		const auto* parameter = find(identifier);
		REQUIRE(parameter != nullptr);
		REQUIRE(parameter->getNormalisableRange().start == Catch::Approx(-100.0f));
		REQUIRE(parameter->getNormalisableRange().end == Catch::Approx(100.0f));
	}
}
