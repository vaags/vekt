#include <vekt/kobber/PluginProcessor.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>

TEST_CASE("Kobber sound parameters exclude resource configuration", "[kobber][parameters]")
{
	using namespace vekt::kobber::parameters;
	REQUIRE(juce::String(presetProductIdentifier) == "com.vekt.kobber");
	REQUIRE(std::find(soundParameterIds.begin(), soundParameterIds.end(), filterCutoff) != soundParameterIds.end());
	REQUIRE(std::find(soundParameterIds.begin(), soundParameterIds.end(), heldKeyReturn) != soundParameterIds.end());
	REQUIRE(std::find(soundParameterIds.begin(), soundParameterIds.end(), filterQCompensation) != soundParameterIds.end());
	REQUIRE(std::find(soundParameterIds.begin(), soundParameterIds.end(), ampRelease) != soundParameterIds.end());
	REQUIRE(std::find(soundParameterIds.begin(), soundParameterIds.end(), filterRelease) != soundParameterIds.end());
	REQUIRE(std::find(soundParameterIds.begin(), soundParameterIds.end(), voiceCount) == soundParameterIds.end());
	REQUIRE(std::find(soundParameterIds.begin(), soundParameterIds.end(), trackingOversampling) == soundParameterIds.end());
	REQUIRE(std::find(soundParameterIds.begin(), soundParameterIds.end(), offlineOversampling) == soundParameterIds.end());
}

TEST_CASE("Kobber exposes only independent amp and filter release controls", "[kobber][parameters][contour]")
{
	vekt::kobber::PluginProcessor processor;
	REQUIRE(processor.getParameters().getParameter("contourCurve") == nullptr);
	REQUIRE(processor.getParameters().getParameter("releasePolicy") == nullptr);
	REQUIRE(processor.getParameters().getParameter(vekt::kobber::parameters::ampRelease) != nullptr);
	REQUIRE(processor.getParameters().getParameter(vekt::kobber::parameters::filterRelease) != nullptr);
}

TEST_CASE("Kobber oscillator tuning exposes musical octave and cents ranges", "[kobber][parameters]")
{
	vekt::kobber::PluginProcessor processor;
	const auto find = [&processor](const char* identifier)
	{
		return dynamic_cast<juce::RangedAudioParameter*>(processor.getParameters().getParameter(identifier));
	};
	for (const auto* identifier : { vekt::kobber::parameters::osc1Octave, vekt::kobber::parameters::osc2Octave, vekt::kobber::parameters::osc3Octave })
	{
		const auto* parameter = find(identifier);
		REQUIRE(parameter != nullptr);
		REQUIRE(parameter->getNormalisableRange().start == Catch::Approx(-2.0f));
		REQUIRE(parameter->getNormalisableRange().end == Catch::Approx(2.0f));
		REQUIRE(parameter->getNormalisableRange().interval == Catch::Approx(1.0f));
	}
	for (const auto* identifier : { vekt::kobber::parameters::osc1Fine, vekt::kobber::parameters::osc2Fine, vekt::kobber::parameters::osc3Fine })
	{
		const auto* parameter = find(identifier);
		REQUIRE(parameter != nullptr);
		REQUIRE(parameter->getNormalisableRange().start == Catch::Approx(-100.0f));
		REQUIRE(parameter->getNormalisableRange().end == Catch::Approx(100.0f));
	}
}
