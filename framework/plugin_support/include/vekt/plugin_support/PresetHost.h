#pragma once

#include <vekt/presets/FilePresetRepository.h>
#include <vekt/presets/PresetCatalog.h>
#include <vekt/presets/PresetSession.h>
#include <vekt/state/StateManager.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

namespace vekt::plugin_support
{
// A product's presets and project state as the host sees them: the preset catalog (factory presets plus an optional
// user folder), the preset session, the host program API and project save/restore. The processor owns one and
// forwards the host's program and state calls to it; the product supplies its preset descriptor and sound adapter
// (ADR 0010). Message thread only.
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
	// Project metadata saved with the parameters (editor size, product extras such as Rav's stage order).
	[[nodiscard]] juce::ValueTree& metadata() noexcept { return projectState.getMetadata(); }

	[[nodiscard]] juce::Result configureUserPresetDirectory(const juce::File& directory);

	[[nodiscard]] int numPrograms() const noexcept;
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
	state::StateManager projectState;
	std::unique_ptr<presets::FilePresetRepository> userRepository;
	presets::PresetCatalog presetCatalog;
	presets::PresetSession presetSession;
};
}
