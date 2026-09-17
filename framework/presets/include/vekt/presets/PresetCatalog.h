#pragma once

#include <vekt/presets/PresetRepository.h>

#include <optional>
#include <vector>

namespace vekt::presets
{
enum class PresetOrigin
{
	factory,
	user
};

struct PresetEntry final
{
	juce::String name;
	PresetOrigin origin {};
};

class PresetCatalog final
{
public:
	PresetCatalog() = default;
	explicit PresetCatalog(PresetRepository& userRepository);

	[[nodiscard]] juce::Result addFactoryPreset(const juce::String& json);
	void setUserRepository(PresetRepository* userRepository);
	void refresh();

	[[nodiscard]] const std::vector<PresetEntry>& entries() const noexcept;
	[[nodiscard]] juce::Result load(std::size_t index, Preset& destination) const;
	[[nodiscard]] std::size_t factoryPresetCount() const noexcept;
	[[nodiscard]] juce::String factoryPresetName(std::size_t index) const;
	[[nodiscard]] juce::Result loadFactoryPreset(
		std::size_t index, Preset& destination) const;
	[[nodiscard]] std::optional<std::size_t> findFactoryPreset(
		const juce::String& name) const noexcept;
	[[nodiscard]] std::optional<std::size_t> nextIndex(std::size_t current) const noexcept;
	[[nodiscard]] std::optional<std::size_t> previousIndex(std::size_t current) const noexcept;

private:
	PresetRepository* userPresets {};
	std::vector<Preset> factoryPresets;
	std::vector<PresetEntry> catalogEntries;
};
}
