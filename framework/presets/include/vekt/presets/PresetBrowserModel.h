#pragma once

#include <vekt/presets/PresetCatalog.h>

namespace vekt::presets
{
class PresetBrowserModel final
{
public:
	juce::String search;
	juce::String folder;
	juce::StringArray tags;
	std::optional<PresetOrigin> origin;

	[[nodiscard]] std::vector<PresetEntry> filter(const std::vector<PresetEntry>& entries) const
	{
		std::vector<PresetEntry> result;
		for (const auto& entry : entries)
		{
			if (origin && entry.origin != *origin) continue;
			if (folder.isNotEmpty() && entry.folder != folder && !entry.folder.startsWith(folder + "/")) continue;
			bool matches = true;
			for (const auto& tag : normaliseTags(tags))
				if (!entry.tags.contains(tag, true)) matches = false;
			const auto text = entry.name + " " + entry.folder + " " + entry.tags.joinIntoString(" ");
			juce::StringArray words;
			words.addTokens(search.trim(), true);
			for (const auto& word : words)
				if (!text.containsIgnoreCase(word)) matches = false;
			if (matches) result.push_back(entry);
		}
		return result;
	}
};
}