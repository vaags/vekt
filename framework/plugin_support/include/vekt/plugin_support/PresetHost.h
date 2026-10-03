#pragma once

#include <vekt/presets/FilePresetRepository.h>
#include <vekt/presets/PresetCatalog.h>
#include <vekt/presets/PresetSession.h>
#include <vekt/state/StateManager.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <mutex>
#include <type_traits>

namespace vekt::plugin_support
{
// The undo history belongs to the editor, which uses it on the message thread. A preset a host applies from another
// thread is not undoable and leaves the history alone: the history on the message thread (or with no message
// manager), otherwise nullptr.
[[nodiscard]] juce::UndoManager* editorUndo(juce::UndoManager& history) noexcept;

// A product's presets and project state as the host sees them: the preset catalog (factory presets plus an optional
// user folder), the preset session, the host program API and project save/restore. The processor owns one and
// forwards the host's program and state calls to it; the product supplies its preset descriptor and sound adapter
// (ADR 0010). Thread-safe: hosts may save, restore and switch programs from any thread (AU hosts and auval do, at the
// same time), so these and the preset session take one recursive lock; the audio thread never takes it. Editor-only
// operations (the user folder, preset navigation) stay on the message thread.
//
// The host program bank is the factory presets. The current program is the selected factory preset, or 0 while a
// user preset or nothing is selected. Projects save the session's selection as "vektPresetSelection".
class PresetHost final
{
public:
	PresetHost(juce::AudioProcessorValueTreeState& parameters, presets::PresetProduct product,
		presets::PresetSoundAdapter sound, int projectSchemaVersion = 1);

	[[nodiscard]] presets::PresetCatalog& catalog() noexcept { return presetCatalog; }
	[[nodiscard]] presets::PresetSession& session() noexcept { return presetSession; }
	[[nodiscard]] const presets::PresetSession& session() const noexcept { return presetSession; }
	// Project metadata saved with the parameters (editor size, product extras such as Rav's stage order), read and
	// written under the state lock.
	[[nodiscard]] juce::var metadataValue(const juce::Identifier& name) const;
	void setMetadataValue(const juce::Identifier& name, const juce::var& value);
	[[nodiscard]] juce::ValueTree metadataCopy() const;
	// Runs function(metadata) under the state lock, for a product extra kept in step with the metadata. The function
	// must not keep the tree: it returns a value, never a reference into the metadata.
	template <typename Function>
	auto withMetadata(Function&& function)
	{
		static_assert(!std::is_reference_v<std::invoke_result_t<Function, juce::ValueTree&>>);
		const std::scoped_lock locked(stateMutex);
		return function(projectState.getMetadata());
	}
	// Holds the state lock, so a product can keep its own state in step with a save or restore.
	[[nodiscard]] std::unique_lock<std::recursive_mutex> lockState() const { return std::unique_lock(stateMutex); }

	[[nodiscard]] juce::Result configureUserPresetDirectory(const juce::File& directory);

	[[nodiscard]] int numPrograms() const;
	[[nodiscard]] int currentProgram() const;
	// Loads factory preset `index`; out-of-range indices change nothing.
	void selectProgram(int index);
	[[nodiscard]] juce::String programName(int index) const;

	// The next or previous preset in catalog order, wrapping; with nothing selected, the first or last.
	[[nodiscard]] juce::Result loadAdjacentPreset(bool next);

	void save(juce::MemoryBlock& destination);
	// All-or-nothing: false leaves parameters, metadata and selection unchanged.
	[[nodiscard]] bool restore(const void* data, int size);

	inline static constexpr auto selectionProperty = "vektPresetSelection";
	// Earlier projects also named the selected factory preset here; restore removes it (ADR 0010).
	inline static constexpr auto legacyFactoryPresetProperty = "currentFactoryPreset";

private:
	mutable std::recursive_mutex stateMutex;
	state::StateManager projectState;
	std::unique_ptr<presets::FilePresetRepository> userRepository;
	presets::PresetCatalog presetCatalog;
	presets::PresetSession presetSession;
};
}
