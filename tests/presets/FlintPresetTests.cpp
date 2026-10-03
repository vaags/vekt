#include "KickClassicAnalog.h"
#include "MalletBar.h"

#include <vekt/flint/Parameters.h>
#include <vekt/flint/PluginProcessor.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <map>

namespace
{
namespace parameters = vekt::flint::parameters;
using vekt::flint::KickClassicAnalog;
using vekt::flint::MalletBar;
using vekt::flint::Mode;

void setParameter(vekt::flint::PluginProcessor& processor, const char* identifier, float value)
{
	auto* parameter = processor.getParameters().getParameter(identifier);
	REQUIRE(parameter != nullptr);
	parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

float getParameter(vekt::flint::PluginProcessor& processor, const char* identifier)
{
	const auto* parameter = processor.getParameters().getRawParameterValue(identifier);
	REQUIRE(parameter != nullptr);
	return parameter->load();
}

double amount(vekt::flint::PluginProcessor& processor, const char* identifier)
{
	return static_cast<double>(getParameter(processor, identifier)) / 100.0;
}

// The factory presets' targets (docs/FLINT_VALIDATION.md, the model sheets), in the sheets' units.
void checkTarget(vekt::flint::PluginProcessor& processor, const juce::String& name)
{
	const auto mode = static_cast<Mode>(juce::roundToInt(getParameter(processor, parameters::mode)));
	const auto driveType = juce::roundToInt(getParameter(processor, parameters::driveType));
	const auto kickT60 = KickClassicAnalog::t60Seconds(amount(processor, parameters::decay));
	const auto barT60 = MalletBar::fundamentalT60(amount(processor, parameters::decay),
	    MalletBar::pitchHz(static_cast<double>(getParameter(processor, parameters::pitch))));
	const auto value = [&](const char* identifier) { return getParameter(processor, identifier); };
	if (name == "Classic")
	{
		REQUIRE(mode == Mode::kick);
		const auto start = parameters::startValuesFor(Mode::kick);
		REQUIRE(value(parameters::pitch) == Catch::Approx(start.pitch));
		REQUIRE(value(parameters::decay) == Catch::Approx(start.decay));
		for (const auto* identifier : { parameters::kickSweep, parameters::kickClick, parameters::kickBodyShape })
			REQUIRE(value(identifier) == Catch::Approx(0.0f));
	}
	else if (name == "Long Boom")
	{
		REQUIRE(mode == Mode::kick);
		REQUIRE(kickT60 == Catch::Approx(3.0).epsilon(0.02));
		REQUIRE(value(parameters::kickBodyShape) == Catch::Approx(30.0f));
		REQUIRE(value(parameters::drive) == Catch::Approx(20.0f));
		REQUIRE(driveType == 0);
	}
	else if (name == "Tight")
	{
		REQUIRE(mode == Mode::kick);
		REQUIRE(kickT60 == Catch::Approx(0.12).epsilon(0.02));
		REQUIRE(KickClassicAnalog::sweepSemitones(amount(processor, parameters::kickSweep)) ==
		    Catch::Approx(3.0).epsilon(0.02));
		REQUIRE(KickClassicAnalog::sweepTimeSeconds(amount(processor, parameters::kickSweepTime)) ==
		    Catch::Approx(0.015).epsilon(0.02));
		REQUIRE(value(parameters::kickClick) == Catch::Approx(30.0f));
	}
	else if (name == "Distorted Sub")
	{
		REQUIRE(mode == Mode::kick);
		REQUIRE(kickT60 == Catch::Approx(1.5).epsilon(0.02));
		REQUIRE(value(parameters::drive) == Catch::Approx(35.0f));
		REQUIRE(driveType == 2);
	}
	else if (name == "Rosewood Marimba")
	{
		REQUIRE(mode == Mode::mallet);
		REQUIRE(value(parameters::pitch) == Catch::Approx(48.0f)); // C3
		const auto start = parameters::startValuesFor(Mode::mallet);
		REQUIRE(value(parameters::decay) == Catch::Approx(start.decay));
		REQUIRE(value(parameters::barMaterial) == Catch::Approx(20.0f));
		REQUIRE(value(parameters::barOvertones) == Catch::Approx(100.0f));
	}
	else if (name == "Xylophone")
	{
		REQUIRE(mode == Mode::mallet);
		REQUIRE(value(parameters::pitch) == Catch::Approx(72.0f)); // C5
		REQUIRE(value(parameters::barOvertones) == Catch::Approx(50.0f));
		REQUIRE(value(parameters::barHardness) == Catch::Approx(80.0f));
		REQUIRE(value(parameters::barMaterial) == Catch::Approx(30.0f));
		REQUIRE(value(parameters::barResonator) == Catch::Approx(20.0f));
	}
	else if (name == "Vibraphone")
	{
		REQUIRE(mode == Mode::mallet);
		REQUIRE(value(parameters::pitch) == Catch::Approx(53.0f)); // F3
		REQUIRE(barT60 == Catch::Approx(6.0).epsilon(0.02));
		REQUIRE(value(parameters::barMaterial) == Catch::Approx(90.0f));
		REQUIRE(value(parameters::barResonator) == Catch::Approx(40.0f));
	}
	else if (name == "Glockenspiel")
	{
		REQUIRE(mode == Mode::mallet);
		REQUIRE(value(parameters::pitch) == Catch::Approx(84.0f)); // C6
		REQUIRE(value(parameters::barMaterial) == Catch::Approx(100.0f));
		REQUIRE(value(parameters::barOvertones) == Catch::Approx(0.0f));
		REQUIRE(value(parameters::barHardness) == Catch::Approx(90.0f));
		REQUIRE(value(parameters::barResonator) == Catch::Approx(0.0f));
	}
	else
		FAIL("no target for factory preset " << name);
}
}

TEST_CASE("Flint factory bank loads complete sound-only presets that meet their targets", "[flint][presets]")
{
	juce::ScopedJuceInitialiser_GUI initialiseJuce;
	vekt::flint::PluginProcessor processor;
	auto& session = processor.getPresetSession();
	auto& catalog = session.library();
	REQUIRE(catalog.factoryPresetCount() == 8);
	setParameter(processor, parameters::trackingOversampling, 2.0f);
	setParameter(processor, parameters::offlineOversampling, 3.0f);
	const auto seed = processor.getSeed();
	for (std::size_t index = 0; index < catalog.factoryPresetCount(); ++index)
	{
		vekt::presets::Preset preset;
		REQUIRE(catalog.loadFactoryPreset(index, preset).wasOk());
		INFO(preset.name.toStdString());
		// Every sound parameter, the other mode's included, so a preset sounds the same whatever was set before.
		REQUIRE(preset.parameters.size() == parameters::soundParameterIds.size());
		REQUIRE(session.load(preset.identifier, vekt::presets::PresetOrigin::factory).wasOk());
		REQUIRE(session.loaded()->name == preset.name);
		REQUIRE(processor.getCurrentProgram() == static_cast<int>(index));
		REQUIRE(processor.getProgramName(static_cast<int>(index)) == preset.name);
		REQUIRE_FALSE(session.modified());
		checkTarget(processor, preset.name);
		// Quality and the seed belong to the project, not the preset.
		REQUIRE(getParameter(processor, parameters::trackingOversampling) == Catch::Approx(2.0f));
		REQUIRE(getParameter(processor, parameters::offlineOversampling) == Catch::Approx(3.0f));
		REQUIRE(processor.getSeed() == seed);
		setParameter(processor, parameters::variation, 77.0f);
		REQUIRE(session.modified());
	}
}

TEST_CASE("Flint rejects presets from another product", "[flint][presets]")
{
	juce::ScopedJuceInitialiser_GUI initialiseJuce;
	vekt::flint::PluginProcessor processor;
	auto preset = processor.createPreset("Wrong product");
	preset.productIdentifier = "com.vekt.glimmer";
	REQUIRE(processor.applyPreset(preset).failed());
}

TEST_CASE("Flint rejects malformed presets without changing sound", "[flint][presets]")
{
	juce::ScopedJuceInitialiser_GUI initialiseJuce;
	vekt::flint::PluginProcessor processor;
	setParameter(processor, parameters::decay, 30.0f);
	setParameter(processor, parameters::mode, static_cast<float>(Mode::mallet));
	auto preset = processor.createPreset("Invalid");
	setParameter(processor, parameters::decay, 90.0f);
	setParameter(processor, parameters::mode, static_cast<float>(Mode::kick));

	SECTION("missing parameter") { preset.parameters.pop_back(); }
	SECTION("unknown parameter") { preset.parameters.back().identifier = "unknown"; }
	SECTION("out-of-range parameter")
	{
		for (auto& parameter : preset.parameters)
			if (parameter.identifier == parameters::decay) parameter.value = 101.0f;
	}
	SECTION("non-finite parameter")
	{
		for (auto& parameter : preset.parameters)
			if (parameter.identifier == parameters::decay) parameter.value = std::numeric_limits<float>::quiet_NaN();
	}
	SECTION("a mode beyond the list")
	{
		for (auto& parameter : preset.parameters)
			if (parameter.identifier == parameters::mode) parameter.value = static_cast<float>(vekt::flint::modeCount);
	}

	REQUIRE(processor.applyPreset(preset).failed());
	REQUIRE(getParameter(processor, parameters::decay) == Catch::Approx(90.0f));
	REQUIRE(juce::roundToInt(getParameter(processor, parameters::mode)) == static_cast<int>(Mode::kick));
}

TEST_CASE("Flint project recall restores Note Off Damps exactly after fractional host automation", "[flint][presets]")
{
	juce::ScopedJuceInitialiser_GUI initialiseJuce;
	vekt::flint::PluginProcessor processor;
	auto* parameter = processor.getParameters().getParameter(parameters::noteOffDamps);
	for (const auto value : { 0.0f, 1.0f })
	{
		INFO("Note Off Damps = " << value);
		parameter->setValueNotifyingHost(value);
		juce::MemoryBlock state;
		processor.getStateInformation(state);
		parameter->setValueNotifyingHost(value < 0.5f ? 0.280552f : 0.719448f);
		processor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
		REQUIRE(parameter->getValue() == Catch::Approx(value));
	}
}

TEST_CASE("Flint recalls a modified user preset and the seed with the project", "[flint][presets]")
{
	juce::ScopedJuceInitialiser_GUI initialiseJuce;
	struct Directory
	{
		juce::File root = juce::File::getSpecialLocation(juce::File::tempDirectory)
		                      .getChildFile("vekt-flint-presets-" + juce::Uuid().toString());
		~Directory() { root.deleteRecursively(); }
	} directory;
	vekt::flint::PluginProcessor processor;
	REQUIRE(processor.configureUserPresetDirectory(directory.root).wasOk());
	auto& session = processor.getPresetSession();
	REQUIRE(session.loaded()->name == "Classic");
	REQUIRE(processor.loadPreviousPreset().wasOk());
	REQUIRE(session.loaded()->name == "Glockenspiel");
	REQUIRE(processor.loadNextPreset().wasOk());
	REQUIRE(session.loaded()->name == "Classic");
	setParameter(processor, parameters::kickClick, 60.0f);
	REQUIRE(session.save("My Kick", "", { "Kick" }).wasOk());
	REQUIRE(session.origin() == vekt::presets::PresetOrigin::user);
	REQUIRE(processor.getNumPrograms() == 8); // user presets are not host programs (ADR 0010)
	setParameter(processor, parameters::kickBodyShape, 40.0f);
	REQUIRE(session.modified());
	juce::MemoryBlock state;
	processor.getStateInformation(state);
	vekt::flint::PluginProcessor restored;
	restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
	REQUIRE(restored.getPresetSession().loaded().has_value());
	REQUIRE(restored.getPresetSession().loaded()->name == "My Kick");
	REQUIRE(restored.getPresetSession().origin() == vekt::presets::PresetOrigin::user);
	REQUIRE(restored.getPresetSession().modified());
	REQUIRE(getParameter(restored, parameters::kickClick) == Catch::Approx(60.0f));
	REQUIRE(getParameter(restored, parameters::kickBodyShape) == Catch::Approx(40.0f));
	REQUIRE(restored.getSeed() == processor.getSeed());
}
