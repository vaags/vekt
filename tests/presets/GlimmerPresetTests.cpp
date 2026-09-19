#include <vekt/glimmer/Parameters.h>
#include <vekt/glimmer/PluginProcessor.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>

namespace
{
void setParameter(vekt::glimmer::PluginProcessor& processor, const char* identifier, float value)
{
	auto* parameter = processor.getParameters().getParameter(identifier);
	REQUIRE(parameter != nullptr);
	parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

float getParameter(vekt::glimmer::PluginProcessor& processor, const char* identifier)
{
	const auto* parameter = processor.getParameters().getRawParameterValue(identifier);
	REQUIRE(parameter != nullptr);
	return parameter->load();
}
}

TEST_CASE("Glimmer shared presets restore only Glimmer sound parameters", "[glimmer][presets]")
{
	vekt::glimmer::PluginProcessor processor;
	setParameter(processor, vekt::glimmer::parameters::micAngle, 210.0f);
	setParameter(processor, vekt::glimmer::parameters::cabinetModel, 2);
	setParameter(processor, vekt::glimmer::parameters::brake, 1);
	setParameter(processor, vekt::glimmer::parameters::stereoWidth, 143);
	setParameter(processor, vekt::glimmer::parameters::manualSpeedEnabled, 1);
	setParameter(processor, vekt::glimmer::parameters::speedPosition, 67);
	auto preset = processor.createPreset("Rotating Glass");
	setParameter(processor, vekt::glimmer::parameters::micAngle, 30.0f);
	for (const auto* identifier : { vekt::glimmer::parameters::cabinetModel, vekt::glimmer::parameters::brake,
		vekt::glimmer::parameters::stereoWidth, vekt::glimmer::parameters::manualSpeedEnabled, vekt::glimmer::parameters::speedPosition })
		setParameter(processor, identifier, 0);

	REQUIRE(processor.applyPreset(preset).wasOk());
	const auto* angle = processor.getParameters().getRawParameterValue(vekt::glimmer::parameters::micAngle);
	REQUIRE(angle != nullptr);
	REQUIRE(angle->load() == Catch::Approx(210.0f));
	REQUIRE(getParameter(processor, vekt::glimmer::parameters::cabinetModel) == Catch::Approx(2));
	REQUIRE(getParameter(processor, vekt::glimmer::parameters::brake) == Catch::Approx(1));
	REQUIRE(getParameter(processor, vekt::glimmer::parameters::stereoWidth) == Catch::Approx(143));
	REQUIRE(getParameter(processor, vekt::glimmer::parameters::manualSpeedEnabled) == Catch::Approx(1));
	REQUIRE(getParameter(processor, vekt::glimmer::parameters::speedPosition) == Catch::Approx(67));
}

TEST_CASE("Glimmer rejects presets from another product", "[glimmer][presets]")
{
	vekt::glimmer::PluginProcessor processor;
	auto preset = processor.createPreset("Wrong product");
	preset.productIdentifier = "com.vekt.rav";
	REQUIRE(processor.applyPreset(preset).failed());
}

TEST_CASE("Glimmer rejects malformed presets without changing sound", "[glimmer][presets]")
{
	vekt::glimmer::PluginProcessor processor;
	setParameter(processor, vekt::glimmer::parameters::micAngle, 30.0f);
	auto preset = processor.createPreset("Invalid");
	setParameter(processor, vekt::glimmer::parameters::micAngle, 90.0f);

	SECTION("missing parameter")
	{
		preset.parameters.pop_back();
	}
	SECTION("unknown parameter")
	{
		preset.parameters.back().identifier = "unknown";
	}
	SECTION("out-of-range parameter")
	{
		for (auto& parameter : preset.parameters)
			if (parameter.identifier == vekt::glimmer::parameters::micAngle)
				parameter.value = 361.0f;
	}
	SECTION("non-finite parameter")
	{
		for (auto& parameter : preset.parameters)
			if (parameter.identifier == vekt::glimmer::parameters::micAngle)
				parameter.value = std::numeric_limits<float>::quiet_NaN();
	}

	REQUIRE(processor.applyPreset(preset).failed());
	REQUIRE(getParameter(processor, vekt::glimmer::parameters::micAngle) == Catch::Approx(90.0f));
}
