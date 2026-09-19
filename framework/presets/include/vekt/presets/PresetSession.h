#pragma once

#include <vekt/presets/PresetCatalog.h>
#include <vekt/presets/PresetDocument.h>
#include <vekt/presets/PresetJsonCodec.h>
#include <functional>

namespace vekt::presets
{
struct PresetProduct final
{
	juce::String identifier;
	juce::String displayName;
	int soundSchemaVersion { 1 };
};

// All callbacks run on the message thread. validate/migrate must not mutate
// live sound; apply must be all-or-nothing and create one undo transaction.
struct PresetSoundAdapter final
{
	std::function<Preset(const juce::String&)> capture;
	std::function<juce::Result(Preset&)> migrate;
	std::function<juce::Result(const Preset&)> validate;
	std::function<juce::Result(const Preset&)> apply;
	std::function<bool(const Preset&)> matches;
};

class PresetSession final
{
public:
	PresetSession(PresetCatalog& library, PresetProduct descriptor, PresetSoundAdapter sound)
		: catalog(library), product(std::move(descriptor)), adapter(std::move(sound))
	{
		catalog.setProductIdentifier(product.identifier);
	}

	[[nodiscard]] juce::Result prepare(Preset& preset) const
	{
		if (const auto result = PresetDocument::validate(preset); result.failed()) return result;
		if (preset.productIdentifier != product.identifier)
			return juce::Result::fail("Preset belongs to a different product");
		if (preset.soundSchemaVersion > product.soundSchemaVersion)
			return juce::Result::fail("Preset requires a newer version of this product");
		if (preset.soundSchemaVersion < product.soundSchemaVersion)
		{
			if (!adapter.migrate) return juce::Result::fail("Sound migration is unavailable");
			if (const auto result = adapter.migrate(preset); result.failed()) return result;
		}
		if (preset.soundSchemaVersion != product.soundSchemaVersion || !adapter.validate)
			return juce::Result::fail("Unsupported sound schema");
		if (const auto result = PresetDocument::validate(preset); result.failed()) return result;
		if (preset.productIdentifier != product.identifier)
			return juce::Result::fail("Migration changed product identity");
		return adapter.validate(preset);
	}

	[[nodiscard]] juce::Result load(const juce::String& id, PresetOrigin origin)
	{
		const auto index = catalog.findById(id, origin);
		if (!index) return juce::Result::fail("Preset is no longer available");
		Preset preset;
		if (const auto result = catalog.load(*index, preset); result.failed()) return result;
		if (preset.identifier != id)
			return juce::Result::fail("Preset changed on disk; refresh the library and try again");
		if (const auto result = prepare(preset); result.failed()) return result;
		if (!adapter.apply) return juce::Result::fail("Sound application is unavailable");
		if (const auto result = adapter.apply(preset); result.failed()) return result;
		adopt(preset, origin);
		return juce::Result::ok();
	}

	[[nodiscard]] juce::Result save(const juce::String& name, const juce::String& folder,
		const juce::StringArray& tags, PresetSaveMode mode = PresetSaveMode::createOnly)
	{
		if (!adapter.capture) return juce::Result::fail("Sound capture is unavailable");
		auto preset = adapter.capture(name);
		preset.folder = folder;
		preset.tags = normaliseTags(tags);
		preset.productIdentifier = product.identifier;
		preset.soundSchemaVersion = product.soundSchemaVersion;
		preset.identifier = juce::Uuid().toString();
		if (snapshot) preset.metadata = snapshot->metadata;
		const auto location = folder.isEmpty() ? name.trim() : folder + "/" + name.trim();
		if (mode == PresetSaveMode::replaceExisting)
		{
			const auto existing = catalog.find(location, PresetOrigin::user);
			if (!existing) return juce::Result::fail("Preset to replace no longer exists");
			Preset previous;
			if (const auto result = catalog.load(*existing, previous); result.failed()) return result;
			preset.identifier = previous.identifier;
			preset.metadata = previous.metadata;
		}
		if (const auto result = prepare(preset); result.failed()) return result;
		if (const auto result = catalog.saveUserPreset(preset, mode); result.failed()) return result;
		adopt(preset, PresetOrigin::user);
		return juce::Result::ok();
	}

	void adopt(const Preset& preset, PresetOrigin origin)
	{
		snapshot = preset;
		loadedOrigin = origin;
		if (onSelectionChanged) onSelectionChanged();
	}
	void clear() { snapshot.reset(); if (onSelectionChanged) onSelectionChanged(); }
	std::function<void()> onSelectionChanged;
	[[nodiscard]] PresetOrigin origin() const noexcept { return loadedOrigin; }
	// Project metadata stores the comparison baseline, never substitutes for the
	// project's live sound. Restoring does not read files or apply parameters.
	[[nodiscard]] juce::String selectionState() const
	{
		if (!snapshot) return {};
		juce::String json;
		if (PresetJsonCodec::encode(*snapshot, json).failed()) return {};
		auto object = std::make_unique<juce::DynamicObject>();
		object->setProperty("origin", loadedOrigin == PresetOrigin::factory ? "factory" : "user");
		object->setProperty("preset", json);
		return juce::JSON::toString(juce::var(object.release()));
	}
	[[nodiscard]] juce::Result restoreSelection(const juce::String& state)
	{
		if (state.isEmpty()) { clear(); return juce::Result::ok(); }
		const auto parsed = juce::JSON::parse(state);
		const auto originName = parsed.getProperty("origin", {}).toString();
		if (originName != "factory" && originName != "user")
			return juce::Result::fail("Invalid saved preset selection");
		Preset preset;
		if (const auto result = PresetJsonCodec::decode(parsed.getProperty("preset", {}).toString(), preset); result.failed()) return result;
		if (const auto result = prepare(preset); result.failed()) return result;
		adopt(preset, originName == "factory" ? PresetOrigin::factory : PresetOrigin::user);
		return juce::Result::ok();
	}
	[[nodiscard]] juce::Result updateTags(const juce::String& id, const juce::StringArray& tags)
	{
		const auto index = catalog.findById(id, PresetOrigin::user);
		if (!index) return juce::Result::fail("Select a user preset to edit its tags");
		Preset preset;
		if (const auto result = catalog.load(*index, preset); result.failed()) return result;
		preset.tags = normaliseTags(tags);
		if (const auto result = catalog.saveUserPreset(preset, PresetSaveMode::replaceExisting); result.failed()) return result;
		if (snapshot && snapshot->identifier == id) snapshot->tags = preset.tags;
		return juce::Result::ok();
	}
	[[nodiscard]] juce::Result importFile(const juce::File& file, const juce::String& folder)
	{
		if (!file.existsAsFile() || file.getSize() > 4 * 1024 * 1024)
			return juce::Result::fail("Select a preset file smaller than 4 MB");
		Preset preset;
		if (const auto result = PresetJsonCodec::decode(file.loadFileAsString(), preset); result.failed()) return result;
		if (const auto result = prepare(preset); result.failed()) return result;
		preset.identifier = juce::Uuid().toString();
		preset.folder = folder;
		return catalog.saveUserPreset(preset);
	}
	[[nodiscard]] juce::Result exportFile(const juce::String& id, PresetOrigin origin, const juce::File& file)
	{
		const auto index = catalog.findById(id, origin);
		if (!index) return juce::Result::fail("Preset is no longer available");
		Preset preset;
		if (const auto result = catalog.load(*index, preset); result.failed()) return result;
		juce::String json;
		if (const auto result = PresetJsonCodec::encode(preset, json); result.failed()) return result;
		juce::TemporaryFile temporary(file);
		if (!temporary.getFile().replaceWithText(json) || !temporary.overwriteTargetFileWithTemporary())
			return juce::Result::fail("Could not export preset");
		return juce::Result::ok();
	}
	[[nodiscard]] bool modified() const { return snapshot && adapter.matches && !adapter.matches(*snapshot); }
	[[nodiscard]] const std::optional<Preset>& loaded() const noexcept { return snapshot; }
	[[nodiscard]] std::optional<std::size_t> currentIndex() const
	{ return snapshot ? catalog.findById(snapshot->identifier, loadedOrigin) : std::nullopt; }
	[[nodiscard]] PresetCatalog& library() noexcept { return catalog; }
	[[nodiscard]] const PresetProduct& descriptor() const noexcept { return product; }

private:
	PresetCatalog& catalog;
	PresetProduct product;
	PresetSoundAdapter adapter;
	std::optional<Preset> snapshot;
	PresetOrigin loadedOrigin {};
};
}