#include <vekt/presets/PresetBrowserModel.h>
#include <vekt/presets/PresetSession.h>
#include <vekt/presets/FilePresetRepository.h>
#include <vekt/presets/PresetJsonCodec.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

namespace
{
struct Directory
{
	juce::File root = juce::File::getSpecialLocation(juce::File::tempDirectory)
		.getNonexistentChildFile("vekt-framework", {}, false);
	~Directory() { root.deleteRecursively(); }
};

struct TestProduct
{
	juce::String id, parameter;
	float value = 0.5f;
	int version = 1;
	vekt::presets::Preset capture(const juce::String& name) const
	{
		vekt::presets::Preset result;
		result.productIdentifier = id; result.name = name;
		result.soundSchemaVersion = version;
		result.parameters.push_back({ parameter, value });
		return result;
	}
	vekt::presets::PresetSoundAdapter adapter()
	{
		return {
			[this](const juce::String& name) { return capture(name); },
			[this](vekt::presets::Preset& preset) { preset.soundSchemaVersion = version; return juce::Result::ok(); },
			[this](const vekt::presets::Preset& preset)
			{
				return preset.parameters.size() == 1 && preset.parameters[0].identifier == parameter
					&& preset.parameters[0].value >= 0 && preset.parameters[0].value <= 1
					? juce::Result::ok() : juce::Result::fail("Invalid test sound");
			},
			[this](const vekt::presets::Preset& preset) { value = preset.parameters[0].value; return juce::Result::ok(); },
			[this](const vekt::presets::Preset& preset) { return std::abs(value - preset.parameters[0].value) < 1.0e-6f; }
		};
	}
};
}

TEST_CASE("Global preset format round trips independent product schemas", "[presets][framework]")
{
	for (const auto& product : { TestProduct { "test.drive", "gain" }, TestProduct { "test.delay", "feedback" } })
	{
		auto source = product.capture("Example");
		source.tags = { " Warm ", "warm", "Bass" };
		source.folder = "Not/Portable";
		source.soundState.set("routing", "serial");
		source.metadata.set("author", "Example author");
		juce::String json;
		REQUIRE(vekt::presets::PresetJsonCodec::encode(source, json).wasOk());
		REQUIRE_FALSE(json.contains("Not/Portable"));
		vekt::presets::Preset restored;
		REQUIRE(vekt::presets::PresetJsonCodec::decode(json, restored).wasOk());
		REQUIRE(restored.identifier == source.identifier);
		REQUIRE(restored.productIdentifier == product.id);
		REQUIRE(restored.tags.size() == 2);
		REQUIRE(restored.soundState["routing"].toString() == "serial");
		REQUIRE(restored.metadata["author"].toString() == "Example author");
	}
}

TEST_CASE("Legacy global documents migrate without a plugin dependency", "[presets][framework]")
{
	vekt::presets::Preset preset;
	REQUIRE(vekt::presets::PresetJsonCodec::decode(
		R"({"schemaVersion":1,"product":"test.old","name":"Old","parameters":{"amount":0.5},"metadata":{"category":"Warm"}})", preset).wasOk());
	REQUIRE(preset.schemaVersion == 2);
	REQUIRE(preset.soundSchemaVersion == 1);
	REQUIRE(preset.tags.contains("Warm"));
	const auto id = preset.identifier;
	REQUIRE(vekt::presets::PresetJsonCodec::decode("{}", preset).failed());
	REQUIRE(preset.identifier == id);
}

TEST_CASE("Folder repositories retain identity and reject unsafe locations", "[presets][framework]")
{
	Directory directory;
	vekt::presets::FilePresetRepository repository(directory.root);
	auto preset = TestProduct { "test.drive", "gain" }.capture("Same name");
	preset.folder = "Bass/Soft";
	preset.tags = { "Warm" };
	REQUIRE(repository.save(preset).wasOk());
	preset.folder = "Drums";
	preset.identifier = juce::Uuid().toString();
	REQUIRE(repository.save(preset).wasOk());
	REQUIRE(repository.list().size() == 2);
	REQUIRE(repository.folders().contains("Bass/Soft"));
	REQUIRE(repository.move("Drums/Same name", "Drums/Renamed").wasOk());
	vekt::presets::Preset loaded;
	REQUIRE(repository.load("Drums/Renamed", loaded).wasOk());
	REQUIRE(loaded.identifier == preset.identifier);
	REQUIRE(loaded.name == "Renamed");
	REQUIRE(loaded.tags.contains("Warm"));
	REQUIRE(repository.removeEmptyFolder("Bass").failed());
	REQUIRE(repository.createFolder("../Escape").failed());
	REQUIRE(repository.move("Drums/Renamed", "../Escape").failed());
	REQUIRE(repository.load("../Escape", loaded).failed());
	preset.folder = "../Escape";
	REQUIRE(repository.save(preset).failed());
}

TEST_CASE("Shared sessions isolate products and migrate sounds independently", "[presets][framework]")
{
	Directory directory;
	vekt::presets::FilePresetRepository repository(directory.root);
	TestProduct drive { "test.drive", "gain" }, delay { "test.delay", "feedback", 0.25f, 2 };
	vekt::presets::PresetCatalog driveCatalog(repository), delayCatalog(repository);
	vekt::presets::PresetSession driveSession(driveCatalog, { drive.id, "Drive", 1 }, drive.adapter());
	vekt::presets::PresetSession delaySession(delayCatalog, { delay.id, "Delay", 2 }, delay.adapter());
	REQUIRE(driveSession.save("Drive", "Bass", { "Warm" }).wasOk());
	REQUIRE(delaySession.save("Delay", "Echoes", { "Wide" }).wasOk());
	driveCatalog.refresh(); delayCatalog.refresh();
	REQUIRE(driveCatalog.entries().size() == 1);
	REQUIRE(delayCatalog.entries().size() == 1);
	auto incompatible = drive.capture("Wrong");
	REQUIRE(delaySession.prepare(incompatible).failed());
	REQUIRE(delay.value == Catch::Approx(0.25f));
	auto oldDelay = delay.capture("Old"); oldDelay.soundSchemaVersion = 1;
	REQUIRE(delaySession.prepare(oldDelay).wasOk());
	REQUIRE(oldDelay.soundSchemaVersion == 2);
	const auto id = driveSession.loaded()->identifier;
	drive.value = 0.75f;
	REQUIRE(driveSession.modified());
	REQUIRE(driveSession.save("Drive", "Bass", { "Warm", "Bass" }, vekt::presets::PresetSaveMode::replaceExisting).wasOk());
	REQUIRE(driveSession.loaded()->identifier == id);
	REQUIRE_FALSE(driveSession.modified());
	REQUIRE(repository.move("Bass/Drive", "Moved/Drive").wasOk());
	driveCatalog.refresh();
	REQUIRE(driveSession.currentIndex().has_value());
	drive.value = 0;
	REQUIRE(driveSession.load(id, vekt::presets::PresetOrigin::user).wasOk());
	REQUIRE(drive.value == Catch::Approx(0.75f));
}

TEST_CASE("Browser filters combine folder descendants search and all tags", "[presets][framework]")
{
	vekt::presets::PresetBrowserModel model;
	std::vector<vekt::presets::PresetEntry> entries {
		{ "Soft", vekt::presets::PresetOrigin::user, "1", "Bass/Sub/Soft", "Bass/Sub", { "Warm", "Bass" }, {} },
		{ "Hard", vekt::presets::PresetOrigin::user, "2", "Drums/Hard", "Drums", { "Warm" }, {} }
	};
	model.folder = "Bass"; model.tags = { "warm", "bass" }; model.search = "soft";
	REQUIRE(model.filter(entries).size() == 1);
	model.tags.add("Bright");
	REQUIRE(model.filter(entries).empty());
}

TEST_CASE("Tag editing and library import do not apply live sound", "[presets][framework]")
{
	Directory directory;
	vekt::presets::FilePresetRepository repository(directory.root);
	vekt::presets::PresetCatalog catalog(repository);
	TestProduct product { "test.drive", "gain" };
	vekt::presets::PresetSession session(catalog, { product.id, "Drive", 1 }, product.adapter());
	REQUIRE(session.save("Original", {}, { "Warm" }).wasOk());
	const auto id = session.loaded()->identifier;
	product.value = 0.9f;
	REQUIRE(session.updateTags(id, { "Bass" }).wasOk());
	REQUIRE(session.modified());
	REQUIRE(product.value == Catch::Approx(0.9f));
	vekt::presets::Preset saved;
	REQUIRE(repository.load("Original", saved).wasOk());
	REQUIRE(saved.folder.isEmpty());
	REQUIRE(saved.parameters[0].value == Catch::Approx(0.5f));
	REQUIRE(saved.tags.contains("Bass"));
	const auto exported = directory.root.getChildFile("export.json");
	REQUIRE(session.exportFile(id, vekt::presets::PresetOrigin::user, exported).wasOk());
	REQUIRE(session.importFile(exported, "Imported").wasOk());
	REQUIRE(repository.load("Imported/Original", saved).wasOk());
	REQUIRE(saved.identifier != id);
	REQUIRE(product.value == Catch::Approx(0.9f));
	REQUIRE(session.loaded()->identifier == id);
	REQUIRE(session.modified());
}

TEST_CASE("Ambiguous preset identities are unavailable instead of loading the wrong file", "[presets][framework]")
{
	Directory directory;
	vekt::presets::FilePresetRepository repository(directory.root);
	auto preset = TestProduct { "test.drive", "gain" }.capture("One");
	REQUIRE(repository.save(preset).wasOk());
	preset.name = "Two";
	REQUIRE(repository.save(preset).wasOk());
	vekt::presets::PresetCatalog catalog(repository);
	REQUIRE_FALSE(catalog.findById(preset.identifier, vekt::presets::PresetOrigin::user));
	REQUIRE(catalog.entries()[0].error.isNotEmpty());
	vekt::presets::Preset output;
	REQUIRE(catalog.load(0, output).failed());
}

TEST_CASE("Managed legacy moves retain identity after a second move", "[presets][framework]")
{
	Directory directory;
	REQUIRE(directory.root.createDirectory().wasOk());
	REQUIRE(directory.root.getChildFile("Old.vektpreset").replaceWithText(
		R"({"schemaVersion":1,"product":"test.old","name":"Old","parameters":{"gain":0.5}})"));
	vekt::presets::FilePresetRepository repository(directory.root);
	vekt::presets::Preset before, after;
	REQUIRE(repository.load("Old", before).wasOk());
	REQUIRE(repository.move("Old", "Folder/New").wasOk());
	REQUIRE(repository.move("Folder/New", "Final").wasOk());
	REQUIRE(repository.load("Final", after).wasOk());
	REQUIRE(after.identifier == before.identifier);
	REQUIRE(after.folder.isEmpty());
	REQUIRE(after.name == "Final");
}

TEST_CASE("Selection restore preserves live sound without requiring the library file", "[presets][framework]")
{
	Directory directory;
	vekt::presets::FilePresetRepository repository(directory.root);
	vekt::presets::PresetCatalog catalog(repository);
	TestProduct product { "test.drive", "gain" };
	vekt::presets::PresetSession session(catalog, { product.id, "Drive", 1 }, product.adapter());
	REQUIRE(session.save("Saved", {}, {}).wasOk());
	const auto state = session.selectionState();
	REQUIRE(repository.remove("Saved").wasOk());
	catalog.refresh(); session.clear(); product.value = 0.9f;
	REQUIRE(session.restoreSelection(state).wasOk());
	REQUIRE(session.loaded()->name == "Saved");
	REQUIRE_FALSE(session.currentIndex());
	REQUIRE(session.modified());
	REQUIRE(product.value == Catch::Approx(0.9f));
}