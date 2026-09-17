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
	[[nodiscard]] juce::Result save(const Preset& preset) override;
	[[nodiscard]] juce::Result remove(const juce::String& name) override;

	inline static constexpr auto fileExtension = ".vektpreset";

private:
	[[nodiscard]] juce::File fileFor(const juce::String& name) const;

	juce::File rootDirectory;
};
}