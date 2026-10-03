#pragma once

#include <vekt/presets/PresetCatalog.h>
#include <vekt/presets/PresetDocument.h>
#include <vekt/presets/PresetJsonCodec.h>
#include <functional>
#include <mutex>
#include <optional>
#include <type_traits>

namespace vekt::presets
{
struct PresetProduct final
{
	juce::String identifier;
	juce::String displayName;
	int soundSchemaVersion { 1 };
};

// Callbacks run under the session lock, on whichever thread called: the message thread for the editor, any thread for
// a host program change. validate/migrate must not mutate live sound; apply must be all-or-nothing, and creates one
// undo transaction only on the message thread (plugin_support::editorUndo).
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
		const auto locked = lock();
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
		const auto locked = lock();
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
		const juce::StringArray& tags, const juce::String& description = {},
		PresetSaveMode mode = PresetSaveMode::createOnly)
	{
		const auto locked = lock();
		if (!adapter.capture) return juce::Result::fail("Sound capture is unavailable");
		auto preset = adapter.capture(name);
		preset.folder = folder;
		preset.tags = normaliseTags(tags);
		preset.description = description.trim();
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
		const auto locked = lock();
		snapshot = preset;
		loadedOrigin = origin;
		if (onSelectionChanged) onSelectionChanged();
	}
	void clear()
	{
		const auto locked = lock();
		snapshot.reset();
		if (onSelectionChanged) onSelectionChanged();
	}
	std::function<void()> onSelectionChanged;
	[[nodiscard]] PresetOrigin origin() const
	{
		const auto locked = lock();
		return loadedOrigin;
	}
	// Project metadata stores the comparison baseline, never substitutes for the
	// project's live sound. Restoring does not read files or apply parameters.
	// A JSON value, { "origin": "factory" | "user", "preset": <the preset document> }, or void with no selection.
	[[nodiscard]] juce::var selectionState() const
	{
		const auto locked = lock();
		if (!snapshot) return {};
		juce::String json;
		if (PresetJsonCodec::encode(*snapshot, json).failed()) return {};
		auto object = std::make_unique<juce::DynamicObject>();
		object->setProperty("origin", loadedOrigin == PresetOrigin::factory ? "factory" : "user");
		object->setProperty("preset", juce::JSON::parse(json));
		return juce::var(object.release());
	}
	[[nodiscard]] juce::Result restoreSelection(const juce::var& state)
	{
		const auto locked = lock();
		if (state.isVoid()) { clear(); return juce::Result::ok(); }
		const auto originName = state.getProperty("origin", {}).toString();
		if ((originName != "factory" && originName != "user") || !state.getProperty("preset", {}).isObject())
			return juce::Result::fail("Invalid saved preset selection");
		Preset preset;
		if (const auto result = PresetJsonCodec::decode(juce::JSON::toString(state.getProperty("preset", {})), preset); result.failed()) return result;
		if (const auto result = prepare(preset); result.failed()) return result;
		adopt(preset, originName == "factory" ? PresetOrigin::factory : PresetOrigin::user);
		return juce::Result::ok();
	}
	// Rewrites a user preset's tags and description without touching its sound or the live parameters.
	[[nodiscard]] juce::Result updateDetails(
		const juce::String& id, const juce::StringArray& tags, const juce::String& description)
	{
		const auto locked = lock();
		const auto index = catalog.findById(id, PresetOrigin::user);
		if (!index) return juce::Result::fail("Select a user preset to edit its details");
		Preset preset;
		if (const auto result = catalog.load(*index, preset); result.failed()) return result;
		preset.tags = normaliseTags(tags);
		preset.description = description.trim();
		if (const auto result = catalog.saveUserPreset(preset, PresetSaveMode::replaceExisting); result.failed()) return result;
		if (snapshot && loadedOrigin == PresetOrigin::user && snapshot->identifier == id)
		{
			snapshot->tags = preset.tags;
			snapshot->description = preset.description;
		}
		return juce::Result::ok();
	}
	// Deletes a user preset. Deleting the selected one keeps the current sound but clears the selection (PRESET_UX).
	[[nodiscard]] juce::Result removeUserPreset(const PresetEntry& entry)
	{
		const auto locked = lock();
		if (entry.origin != PresetOrigin::user) return juce::Result::fail("Select a user preset to delete");
		const auto identifier = entry.identifier; // removing refreshes the catalog, which may own `entry`
		const auto result = catalog.removeUserPreset(entry.location);
		if (result.wasOk() && snapshot && loadedOrigin == PresetOrigin::user && snapshot->identifier == identifier)
			clear();
		return result;
	}
	[[nodiscard]] juce::Result importFile(const juce::File& file, const juce::String& folder)
	{
		const auto locked = lock();
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
		const auto locked = lock();
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
	[[nodiscard]] bool modified() const
	{
		const auto locked = lock();
		return snapshot && adapter.matches && !adapter.matches(*snapshot);
	}
	// A copy: another thread may change the selection once the lock is released.
	[[nodiscard]] std::optional<Preset> loaded() const
	{
		const auto locked = lock();
		return snapshot;
	}
	[[nodiscard]] std::optional<std::size_t> currentIndex() const
	{
		const auto locked = lock();
		return snapshot ? catalog.findById(snapshot->identifier, loadedOrigin) : std::nullopt;
	}
	// Runs function(catalog) under the session lock, for the editor's preset list and folders while a host restores or
	// switches programs on another thread. It returns a value, never a reference into the catalog.
	template <typename Function>
	auto withLibrary(Function&& function)
	{
		static_assert(!std::is_reference_v<std::invoke_result_t<Function, PresetCatalog&>>);
		const auto locked = lock();
		return function(catalog);
	}
	// Unlocked: for setting up the catalog before a host can call, and for single-threaded tests.
	[[nodiscard]] PresetCatalog& library() noexcept { return catalog; }
	[[nodiscard]] const PresetProduct& descriptor() const noexcept { return product; }
	// Serialises every method above with the owner's other state access (plugin_support::PresetHost), so a host can save,
	// restore and switch programs from any thread while the editor uses the session. Unset, the session is unlocked.
	void useLock(std::recursive_mutex& mutex) noexcept { guard = &mutex; }

private:
	PresetCatalog& catalog;
	PresetProduct product;
	PresetSoundAdapter adapter;
	std::optional<Preset> snapshot;
	PresetOrigin loadedOrigin {};
	std::recursive_mutex* guard {};

	[[nodiscard]] std::unique_lock<std::recursive_mutex> lock() const
	{
		return guard != nullptr ? std::unique_lock(*guard) : std::unique_lock<std::recursive_mutex> {};
	}
};
}