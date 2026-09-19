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
	juce::ScopedJuceInitialiser_GUI initialiseJuce;
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

TEST_CASE("Glimmer factory bank loads complete sound-only presets", "[glimmer][presets]")
{
	juce::ScopedJuceInitialiser_GUI initialiseJuce;
	vekt::glimmer::PluginProcessor processor;
	auto& session = processor.getPresetSession();
	auto& catalog = session.library();
	REQUIRE(catalog.factoryPresetCount() == 6);
	setParameter(processor, vekt::glimmer::parameters::bypass, 1);
	setParameter(processor, vekt::glimmer::parameters::trackingOversampling, 0);
	setParameter(processor, vekt::glimmer::parameters::offlineOversampling, 1);
	for (std::size_t index = 0; index < catalog.factoryPresetCount(); ++index)
	{
		vekt::presets::Preset preset;
		REQUIRE(catalog.loadFactoryPreset(index, preset).wasOk());
		INFO(preset.name.toStdString());
		REQUIRE(preset.parameters.size() == vekt::glimmer::parameters::soundParameterIds.size());
		REQUIRE(session.load(preset.identifier, vekt::presets::PresetOrigin::factory).wasOk());
		REQUIRE(session.loaded()->name == preset.name);
		REQUIRE(processor.getNumPrograms() == 6);
		REQUIRE(processor.getCurrentProgram() == static_cast<int>(index));
		REQUIRE(processor.getProgramName(static_cast<int>(index)) == preset.name);
		REQUIRE_FALSE(session.modified());
		REQUIRE(getParameter(processor, vekt::glimmer::parameters::cabinetModel) == Catch::Approx(static_cast<float>(index / 2)));
		REQUIRE(getParameter(processor, vekt::glimmer::parameters::bypass) == Catch::Approx(1));
		REQUIRE(getParameter(processor, vekt::glimmer::parameters::trackingOversampling) == Catch::Approx(0));
		REQUIRE(getParameter(processor, vekt::glimmer::parameters::offlineOversampling) == Catch::Approx(1));
		setParameter(processor, vekt::glimmer::parameters::brake, 1);
		REQUIRE(session.modified());
	}
}

TEST_CASE("Glimmer rejects presets from another product", "[glimmer][presets]")
{
	juce::ScopedJuceInitialiser_GUI initialiseJuce;
	vekt::glimmer::PluginProcessor processor;
	auto preset = processor.createPreset("Wrong product");
	preset.productIdentifier = "com.vekt.rav";
	REQUIRE(processor.applyPreset(preset).failed());
}

TEST_CASE("Glimmer preset navigation and project recall preserve selection", "[glimmer][presets]")
{
	juce::ScopedJuceInitialiser_GUI initialiseJuce;
	struct Directory
	{
		juce::File root = juce::File::getSpecialLocation(juce::File::tempDirectory)
			.getChildFile("vekt-glimmer-presets-" + juce::Uuid().toString());
		~Directory() { root.deleteRecursively(); }
	} directory;
	vekt::glimmer::PluginProcessor processor;
	REQUIRE(processor.configureUserPresetDirectory(directory.root).wasOk());
	auto& session = processor.getPresetSession();
	REQUIRE(session.loaded()->name == "Classic Chorale");
	REQUIRE_FALSE(session.modified());
	REQUIRE(processor.loadPreviousPreset().wasOk());
	REQUIRE(session.loaded()->name == "Slow Panorama");
	REQUIRE(processor.loadNextPreset().wasOk());
	REQUIRE(session.loaded()->name == "Classic Chorale");
	processor.setCurrentProgram(4);
	REQUIRE(session.loaded()->name == "Glass Motion");
	processor.setCurrentProgram(-1);
	processor.setCurrentProgram(6);
	REQUIRE(processor.getCurrentProgram() == 4);
	REQUIRE(session.save("My Motion", "", { "Custom" }).wasOk());
	REQUIRE(session.origin() == vekt::presets::PresetOrigin::user);
	REQUIRE(processor.getNumPrograms() == 6);
	REQUIRE(processor.getCurrentProgram() == 4);
	REQUIRE(processor.loadNextPreset().wasOk());
	REQUIRE(session.loaded()->name == "Classic Chorale");
	REQUIRE(processor.loadPreviousPreset().wasOk());
	REQUIRE(session.loaded()->name == "My Motion");
	setParameter(processor, vekt::glimmer::parameters::stereoWidth, 120);
	REQUIRE(session.modified());
	juce::MemoryBlock state;
	processor.getStateInformation(state);
	vekt::glimmer::PluginProcessor restored;
	restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
	REQUIRE(restored.getPresetSession().loaded().has_value());
	REQUIRE(restored.getPresetSession().loaded()->name == "My Motion");
	REQUIRE(restored.getPresetSession().origin() == vekt::presets::PresetOrigin::user);
	REQUIRE(restored.getPresetSession().modified());
	REQUIRE(getParameter(restored, vekt::glimmer::parameters::stereoWidth) == Catch::Approx(120));
	REQUIRE(processor.getPresetSession().library().removeUserPreset("My Motion").wasOk());
	session.clear();
	REQUIRE(processor.loadPreviousPreset().wasOk());
	REQUIRE(session.loaded()->name == "Slow Panorama");
}

TEST_CASE("Glimmer project recall restores booleans after fractional host automation", "[glimmer][presets]")
{
	juce::ScopedJuceInitialiser_GUI initialiseJuce;
	vekt::glimmer::PluginProcessor processor;
	for (const auto* identifier : { vekt::glimmer::parameters::brake, vekt::glimmer::parameters::manualSpeedEnabled,
		vekt::glimmer::parameters::autoGain, vekt::glimmer::parameters::bypass })
		for (const auto value : { 0.0f, 1.0f })
		{
			INFO(identifier << " = " << value);
			auto* parameter = processor.getParameters().getParameter(identifier);
			parameter->setValueNotifyingHost(value);
			juce::MemoryBlock state;
			processor.getStateInformation(state);
			parameter->setValueNotifyingHost(value < 0.5f ? 0.280552f : 0.719448f);
			processor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
			REQUIRE(parameter->getValue() == Catch::Approx(value));
		}
}

TEST_CASE("Glimmer rejects malformed presets without changing sound", "[glimmer][presets]")
{
	juce::ScopedJuceInitialiser_GUI initialiseJuce;
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
