#include <vekt/presets/PresetCatalog.h>

#include <vekt/presets/PresetJsonCodec.h>

namespace vekt::presets
{
namespace
{
template <typename Collection>
[[nodiscard]] bool containsName(const Collection& collection, const juce::String& name)
{
	for (const auto& item : collection)
		if (item.name.equalsIgnoreCase(name))
			return true;
	return false;
}
}

PresetCatalog::PresetCatalog(PresetRepository& userRepository)
	: userPresets(&userRepository)
{
	refresh();
}

juce::Result PresetCatalog::addFactoryPreset(const juce::String& json)
{
	Preset preset;
	if (const auto result = PresetJsonCodec::decode(json, preset); result.failed())
		return result;

	if (containsName(factoryPresets, preset.name))
		return juce::Result::fail("Factory preset name is duplicated");

	factoryPresets.push_back(std::move(preset));
	refresh();
	return juce::Result::ok();
}

void PresetCatalog::setUserRepository(PresetRepository* userRepository)
{
	userPresets = userRepository;
	refresh();
}

void PresetCatalog::refresh()
{
	catalogEntries.clear();
	const auto userNames = userPresets != nullptr ? userPresets->list() : juce::StringArray {};
	catalogEntries.reserve(factoryPresets.size() + static_cast<std::size_t>(userNames.size()));

	for (const auto& preset : factoryPresets)
		catalogEntries.push_back({ preset.name, PresetOrigin::factory });

	for (const auto& name : userNames)
		if (!containsName(catalogEntries, name))
			catalogEntries.push_back({ name, PresetOrigin::user });
}

const std::vector<PresetEntry>& PresetCatalog::entries() const noexcept
{
	return catalogEntries;
}

juce::Result PresetCatalog::load(std::size_t index, Preset& destination) const
{
	if (index >= catalogEntries.size())
		return juce::Result::fail("Preset index is out of range");

	if (catalogEntries[index].origin == PresetOrigin::factory)
	{
		destination = factoryPresets[index];
		return juce::Result::ok();
	}

	if (userPresets == nullptr)
		return juce::Result::fail("User preset repository is unavailable");
	return userPresets->load(catalogEntries[index].name, destination);
}

std::size_t PresetCatalog::factoryPresetCount() const noexcept
{
	return factoryPresets.size();
}

juce::String PresetCatalog::factoryPresetName(std::size_t index) const
{
	return index < factoryPresets.size() ? factoryPresets[index].name : juce::String {};
}

juce::Result PresetCatalog::loadFactoryPreset(
	std::size_t index, Preset& destination) const
{
	if (index >= factoryPresets.size())
		return juce::Result::fail("Factory preset index is out of range");

	destination = factoryPresets[index];
	return juce::Result::ok();
}

std::optional<std::size_t> PresetCatalog::findFactoryPreset(
	const juce::String& name) const noexcept
{
	for (std::size_t index = 0; index < factoryPresets.size(); ++index)
		if (factoryPresets[index].name == name)
			return index;

	return std::nullopt;
}

std::optional<std::size_t> PresetCatalog::nextIndex(std::size_t current) const noexcept
{
	if (catalogEntries.empty() || current >= catalogEntries.size())
		return std::nullopt;
	return (current + 1) % catalogEntries.size();
}

std::optional<std::size_t> PresetCatalog::previousIndex(std::size_t current) const noexcept
{
	if (catalogEntries.empty() || current >= catalogEntries.size())
		return std::nullopt;
	return current == 0 ? catalogEntries.size() - 1 : current - 1;
}
}
