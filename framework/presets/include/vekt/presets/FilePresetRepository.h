#pragma once

#include <vekt/presets/PresetRepository.h>

namespace vekt::presets
{
class FilePresetRepository final : public PresetRepository
{
public:
	explicit FilePresetRepository(juce::File directory);

	[[nodiscard]] juce::StringArray list() const override;
	[[nodiscard]] juce::Result load(
		const juce::String& name, Preset& destination) const override;
	[[nodiscard]] juce::Result save(
		const Preset& preset, PresetSaveMode mode = PresetSaveMode::createOnly) override;
	[[nodiscard]] juce::Result remove(const juce::String& name) override;
	[[nodiscard]] juce::StringArray folders() const override;
	[[nodiscard]] juce::Result createFolder(const juce::String& path) override;
	[[nodiscard]] juce::Result move(const juce::String& source, const juce::String& destination) override;
	[[nodiscard]] juce::Result removeEmptyFolder(const juce::String& path);

	inline static constexpr auto fileExtension = ".vektpreset";

private:
	[[nodiscard]] juce::File fileFor(const juce::String& name) const;
	[[nodiscard]] bool safePath(const juce::String& path, bool allowEmpty = false) const;

	juce::File rootDirectory;
};
}
