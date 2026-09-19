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
	return addFactoryPreset(json, {});
}

juce::Result PresetCatalog::addFactoryPreset(const juce::String& json, const juce::String& folder)
{
	Preset preset;
	if (const auto result = PresetJsonCodec::decode(json, preset); result.failed())
		return result;

	if (containsName(factoryPresets, preset.name))
		return juce::Result::fail("Factory preset name is duplicated");
	for (const auto& existing : factoryPresets)
		if (existing.identifier == preset.identifier)
			return juce::Result::fail("Factory preset identity is duplicated");
	if (productIdentifier.isNotEmpty() && preset.productIdentifier != productIdentifier)
		return juce::Result::fail("Factory preset belongs to another product");
	if (folder.contains("..") || juce::File::isAbsolutePath(folder) || folder.containsChar('\\'))
		return juce::Result::fail("Factory preset folder is invalid");
	preset.folder = folder.trim().trimCharactersAtEnd("/");

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
		catalogEntries.push_back({ preset.name, PresetOrigin::factory, preset.identifier,
			preset.folder.isEmpty() ? preset.name : preset.folder + "/" + preset.name,
			preset.folder, preset.tags, {} });

	for (const auto& location : userNames)
	{
		Preset preset;
		const auto result = userPresets->load(location, preset);
		const auto name = location.fromLastOccurrenceOf("/", false, false);
		if (containsName(factoryPresets, name)) continue;
		if (result.wasOk() && productIdentifier.isNotEmpty() && preset.productIdentifier != productIdentifier)
			continue;
		catalogEntries.push_back({ name, PresetOrigin::user,
			result.wasOk() ? preset.identifier : "invalid:" + location, location,
			location.containsChar('/') ? location.upToLastOccurrenceOf("/", false, false) : juce::String {}, preset.tags,
			result.getErrorMessage() });
	}
	// Never resolve an ambiguous ID to whichever file happens to sort first.
	for (std::size_t i = 0; i < catalogEntries.size(); ++i)
		for (std::size_t j = i + 1; j < catalogEntries.size(); ++j)
			if (catalogEntries[i].origin == catalogEntries[j].origin
				&& catalogEntries[i].identifier == catalogEntries[j].identifier)
			{
				catalogEntries[i].error = "Duplicate preset identity; re-import one copy";
				catalogEntries[j].error = catalogEntries[i].error;
			}
}

juce::Result PresetCatalog::saveUserPreset(const Preset& preset, PresetSaveMode mode)
{
	if (userPresets == nullptr)
		return juce::Result::fail("User preset repository is unavailable");
	if (productIdentifier.isNotEmpty() && preset.productIdentifier != productIdentifier)
		return juce::Result::fail("Preset belongs to a different product");
	if (containsName(factoryPresets, preset.name))
		return juce::Result::fail("User preset name conflicts with a factory preset");

	if (const auto result = userPresets->save(preset, mode); result.failed())
		return result;

	refresh();
	return juce::Result::ok();
}

juce::Result PresetCatalog::removeUserPreset(const juce::String& name)
{
	if (userPresets == nullptr)
		return juce::Result::fail("User preset repository is unavailable");
	if (containsName(factoryPresets, name))
		return juce::Result::fail("Factory presets cannot be removed");

	if (const auto result = userPresets->remove(name); result.failed())
		return result;

	refresh();
	return juce::Result::ok();
}

const std::vector<PresetEntry>& PresetCatalog::entries() const noexcept
{
	return catalogEntries;
}

juce::Result PresetCatalog::load(std::size_t index, Preset& destination) const
{
	if (index >= catalogEntries.size())
		return juce::Result::fail("Preset index is out of range");
	if (catalogEntries[index].error.isNotEmpty())
		return juce::Result::fail(catalogEntries[index].error);

	if (catalogEntries[index].origin == PresetOrigin::factory)
	{
		destination = factoryPresets[index];
		return juce::Result::ok();
	}

	if (userPresets == nullptr)
		return juce::Result::fail("User preset repository is unavailable");
	return userPresets->load(catalogEntries[index].location, destination);
}

std::optional<std::size_t> PresetCatalog::find(
	const juce::String& name, PresetOrigin origin) const noexcept
{
	for (std::size_t index = 0; index < catalogEntries.size(); ++index)
		if (catalogEntries[index].origin == origin
			&& catalogEntries[index].location.equalsIgnoreCase(name))
			return index;

	return std::nullopt;
}

std::optional<std::size_t> PresetCatalog::findById(const juce::String& id, PresetOrigin origin) const
{
	std::optional<std::size_t> found;
	for (std::size_t index = 0; index < catalogEntries.size(); ++index)
		if (catalogEntries[index].identifier == id && catalogEntries[index].origin == origin)
		{
			if (found) return std::nullopt;
			found = index;
		}
	return found;
}

juce::StringArray PresetCatalog::folders(PresetOrigin origin) const
{
	juce::StringArray result;
	for (const auto& entry : catalogEntries)
		if (entry.origin == origin && entry.folder.isNotEmpty())
			result.addIfNotAlreadyThere(entry.folder, true);
	result.sortNatural();
	return result;
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
