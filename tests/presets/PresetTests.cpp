#include <FactoryPresets.h>
#include <Parameters.h>
#include <PluginProcessor.h>

#include <vekt/presets/FilePresetRepository.h>
#include <vekt/presets/PresetCatalog.h>
#include <vekt/presets/PresetJsonCodec.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace
{
void setParameter(
	vekt::saturator::PluginProcessor& processor, const char* identifier, float value)
{
	auto* parameter = processor.getParameters().getParameter(identifier);
	REQUIRE(parameter != nullptr);
	parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

float getParameter(
	vekt::saturator::PluginProcessor& processor, const char* identifier)
{
	const auto* parameter = processor.getParameters().getRawParameterValue(identifier);
	REQUIRE(parameter != nullptr);
	return parameter->load();
}

class ScopedTemporaryDirectory final
{
public:
	ScopedTemporaryDirectory()
		: directory(juce::File::getSpecialLocation(juce::File::tempDirectory)
			.getNonexistentChildFile("vekt-preset-tests", {}, true))
	{
	}

	~ScopedTemporaryDirectory()
	{
		directory.deleteRecursively();
	}

	[[nodiscard]] const juce::File& get() const noexcept { return directory; }

private:
	juce::File directory;
};
}

TEST_CASE("Preset documents apply only sound parameters", "[presets]")
{
	vekt::saturator::PluginProcessor source;
	setParameter(source, vekt::saturator::parameters::drive, 18.0f);
	setParameter(source, vekt::saturator::parameters::bias, 0.25f);
	setParameter(source, vekt::saturator::parameters::bypass, 1.0f);
	setParameter(source, vekt::saturator::parameters::oversamplingFactor, 0.0f);

	const auto preset = source.createPreset("Driven");
	REQUIRE(preset.parameters.size()
		== vekt::saturator::parameters::soundParameterIds.size());
	for (const auto& value : preset.parameters)
	{
		REQUIRE(value.identifier != vekt::saturator::parameters::bypass);
		REQUIRE(value.identifier != vekt::saturator::parameters::oversamplingFactor);
		REQUIRE(value.identifier != vekt::saturator::parameters::oversamplingPhase);
	}

	vekt::saturator::PluginProcessor restored;
	setParameter(restored, vekt::saturator::parameters::bypass, 1.0f);
	setParameter(restored, vekt::saturator::parameters::oversamplingFactor, 1.0f);
	restored.getProjectMetadata().setProperty("editorWidth", 900, nullptr);
	const auto result = restored.applyPreset(preset);

	REQUIRE(result.wasOk());
	REQUIRE(getParameter(restored, vekt::saturator::parameters::drive) == Catch::Approx(18.0f));
	REQUIRE(getParameter(restored, vekt::saturator::parameters::bias) == Catch::Approx(0.25f));
	REQUIRE(getParameter(restored, vekt::saturator::parameters::bypass) == Catch::Approx(1.0f));
	REQUIRE(getParameter(restored, vekt::saturator::parameters::oversamplingFactor)
		== Catch::Approx(1.0f));
	REQUIRE(static_cast<int>(restored.getProjectMetadata().getProperty("editorWidth")) == 900);
	REQUIRE(restored.getUndoManager().undo());
	REQUIRE(getParameter(restored, vekt::saturator::parameters::drive) == Catch::Approx(6.0f));
	REQUIRE(getParameter(restored, vekt::saturator::parameters::bias)
		== Catch::Approx(0.0f).margin(1.0e-6f));
	REQUIRE(getParameter(restored, vekt::saturator::parameters::bypass) == Catch::Approx(1.0f));
}

TEST_CASE("Invalid preset documents do not partially change parameters", "[presets]")
{
	vekt::saturator::PluginProcessor processor;
	setParameter(processor, vekt::saturator::parameters::drive, 12.0f);
	auto preset = processor.createPreset("Invalid");
	setParameter(processor, vekt::saturator::parameters::drive, 6.0f);
	preset.parameters.pop_back();

	const auto result = processor.applyPreset(preset);
	REQUIRE(result.failed());
	REQUIRE(getParameter(processor, vekt::saturator::parameters::drive) == Catch::Approx(6.0f));

	vekt::presets::Preset malformedPreset;
	REQUIRE(vekt::presets::PresetJsonCodec::decode(
		R"({"schemaVersion":1,"product":"com.vekt.saturator","name":"Malformed","parameters":{"drive":"loud"}})",
		malformedPreset).failed());
	REQUIRE(vekt::presets::PresetJsonCodec::decode(
		R"({"schemaVersion":1,"product":"com.vekt.saturator","name":"Malformed","parameters":{"drive":true}})",
		malformedPreset).failed());
	REQUIRE(getParameter(processor, vekt::saturator::parameters::drive) == Catch::Approx(6.0f));
}

TEST_CASE("File preset repositories round trip human-readable documents", "[presets]")
{
	ScopedTemporaryDirectory directory;
	vekt::presets::FilePresetRepository repository(directory.get());
	vekt::saturator::PluginProcessor processor;
	juce::NamedValueSet metadata;
	metadata.set("category", "Warm");
	const auto preset = processor.createPreset("Factory Warm", metadata);

	REQUIRE(repository.save(preset).wasOk());
	REQUIRE(repository.list() == juce::StringArray { "Factory Warm" });
	vekt::presets::Preset restored;
	REQUIRE(repository.load("Factory Warm", restored).wasOk());
	REQUIRE(restored.schemaVersion == preset.schemaVersion);
	REQUIRE(restored.productIdentifier == preset.productIdentifier);
	REQUIRE(restored.name == preset.name);
	REQUIRE(restored.parameters.size() == preset.parameters.size());
	for (std::size_t index = 0; index < preset.parameters.size(); ++index)
	{
		REQUIRE(restored.parameters[index].identifier == preset.parameters[index].identifier);
		REQUIRE(restored.parameters[index].value == Catch::Approx(preset.parameters[index].value));
	}
	REQUIRE(restored.metadata["category"].toString() == "Warm");
	const auto text = directory.get().getChildFile("Factory Warm.vektpreset").loadFileAsString();
	REQUIRE(text.trimStart().startsWithChar('{'));
	REQUIRE(text.contains("\"parameters\""));
	REQUIRE_FALSE(text.containsChar('<'));
	REQUIRE(repository.remove("Factory Warm").wasOk());
	REQUIRE(repository.list().isEmpty());
	REQUIRE(repository.save(processor.createPreset("../Factory Warm")).failed());
	REQUIRE(repository.save({}).failed());
}

TEST_CASE("Preset catalogs combine factory and user presets deterministically", "[presets]")
{
	ScopedTemporaryDirectory directory;
	vekt::presets::FilePresetRepository repository(directory.get());
	vekt::saturator::PluginProcessor processor;
	REQUIRE(repository.save(processor.createPreset("User B")).wasOk());
	REQUIRE(repository.save(processor.createPreset("User A")).wasOk());
	REQUIRE(repository.save(processor.createPreset("factory first")).wasOk());

	vekt::presets::Preset factoryPreset = processor.createPreset("Factory First");
	juce::String factoryJson;
	REQUIRE(vekt::presets::PresetJsonCodec::encode(factoryPreset, factoryJson).wasOk());

	vekt::presets::PresetCatalog catalog(repository);
	REQUIRE(catalog.addFactoryPreset(factoryJson).wasOk());
	REQUIRE(catalog.addFactoryPreset(factoryJson).failed());
	REQUIRE(catalog.entries().size() == 3);
	REQUIRE(catalog.entries()[0].name == "Factory First");
	REQUIRE(catalog.entries()[0].origin == vekt::presets::PresetOrigin::factory);
	REQUIRE(catalog.entries()[1].name == "User A");
	REQUIRE(catalog.entries()[1].origin == vekt::presets::PresetOrigin::user);
	REQUIRE(catalog.entries()[2].name == "User B");
	REQUIRE(catalog.factoryPresetCount() == 1);
	REQUIRE(catalog.factoryPresetName(0) == "Factory First");
	REQUIRE(catalog.factoryPresetName(1).isEmpty());
	REQUIRE(catalog.findFactoryPreset("Factory First") == 0);
	REQUIRE_FALSE(catalog.findFactoryPreset("Missing").has_value());

	vekt::presets::Preset loaded;
	REQUIRE(catalog.load(0, loaded).wasOk());
	REQUIRE(loaded.name == "Factory First");
	REQUIRE(catalog.load(1, loaded).wasOk());
	REQUIRE(loaded.name == "User A");
	REQUIRE(catalog.load(3, loaded).failed());
	REQUIRE(catalog.loadFactoryPreset(0, loaded).wasOk());
	REQUIRE(catalog.loadFactoryPreset(1, loaded).failed());
	REQUIRE(catalog.nextIndex(2) == 0);
	REQUIRE(catalog.previousIndex(0) == 2);
}

TEST_CASE("Embedded saturator factory presets use the public preset schema", "[presets]")
{
	ScopedTemporaryDirectory directory;
	vekt::presets::FilePresetRepository repository(directory.get());
	vekt::presets::PresetCatalog catalog(repository);
	vekt::saturator::PluginProcessor processor;

	REQUIRE(vekt::saturator::addFactoryPresets(catalog).wasOk());
	REQUIRE(catalog.entries().size() == 3);
	for (std::size_t index = 0; index < catalog.entries().size(); ++index)
	{
		vekt::presets::Preset preset;
		REQUIRE(catalog.load(index, preset).wasOk());
		REQUIRE(processor.applyPreset(preset).wasOk());
	}
}

TEST_CASE("Saturator exposes factory presets through its host program API", "[presets]")
{
	vekt::saturator::PluginProcessor processor;

	REQUIRE(processor.getNumPrograms() == 3);
	REQUIRE(processor.getCurrentProgram() == 0);
	REQUIRE(processor.getProgramName(0) == "Clean Heat");
	REQUIRE(processor.getProgramName(1) == "Warm Push");
	REQUIRE(processor.getProgramName(2) == "Parallel Grit");
	REQUIRE(processor.getProgramName(3).isEmpty());

	processor.setCurrentProgram(1);
	REQUIRE(processor.getCurrentProgram() == 1);
	REQUIRE(getParameter(processor, vekt::saturator::parameters::drive)
		== Catch::Approx(12.0f));
	processor.setCurrentProgram(8);
	REQUIRE(processor.getCurrentProgram() == 1);
}

TEST_CASE("Saturator restores its current factory program identity", "[presets]")
{
	vekt::saturator::PluginProcessor source;
	source.setCurrentProgram(1);
	juce::MemoryBlock state;
	source.getStateInformation(state);

	vekt::saturator::PluginProcessor restored;
	restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));

	REQUIRE(restored.getCurrentProgram() == 1);
	REQUIRE(restored.getProgramName(restored.getCurrentProgram()) == "Warm Push");
	REQUIRE(getParameter(restored, vekt::saturator::parameters::drive)
		== Catch::Approx(12.0f));
}
