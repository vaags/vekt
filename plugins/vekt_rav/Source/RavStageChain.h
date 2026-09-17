#pragma once

#include "RavModeStage.h"

#include <juce_data_structures/juce_data_structures.h>

#include <array>
#include <cstddef>

namespace vekt::rav
{
class RavStageChain final
{
public:
	inline static constexpr std::size_t stageCount = 6;
	inline static constexpr auto metadataPropertyName = "stageOrder";

	using Order = std::array<RavMode, stageCount>;

	RavStageChain() noexcept
		: order { RavMode::saturation, RavMode::overdrive, RavMode::distortion,
			RavMode::fuzz, RavMode::wavefold, RavMode::bitcrush }
	{
	}

	[[nodiscard]] const Order& getOrder() const noexcept { return order; }

	[[nodiscard]] bool setOrder(Order candidate) noexcept
	{
		std::array<bool, stageCount> seen {};
		for (auto& mode : candidate)
		{
			const auto index = static_cast<std::size_t>(mode);
			if (index >= stageCount || seen[index])
				return false;
			seen[index] = true;
		}
		order = candidate;
		return true;
	}

	void writeMetadata(juce::ValueTree& metadata) const
	{
		juce::StringArray values;
		for (const auto mode : order)
			values.add(juce::String(static_cast<int>(mode)));
		metadata.setProperty(metadataPropertyName, values.joinIntoString(","), nullptr);
	}

	[[nodiscard]] static RavStageChain readMetadata(const juce::ValueTree& metadata)
	{
		RavStageChain chain;
		const auto propertyValue = metadata.getProperty(metadataPropertyName, juce::String {});
		const auto text = propertyValue.toString();
		if (text.isEmpty())
			return chain;

		juce::StringArray tokens;
		tokens.addTokens(text, juce::String { "," }, juce::String {});
		if (tokens.size() != static_cast<int>(stageCount))
			return chain;

		Order candidate {};
		for (int index = 0; index < static_cast<int>(stageCount); ++index)
		{
			const auto value = tokens[index].getIntValue();
			if (value < 0 || value >= static_cast<int>(stageCount))
				return chain;
			candidate[static_cast<std::size_t>(index)] = static_cast<RavMode>(value);
		}

		if (!chain.setOrder(candidate))
			return RavStageChain {};
		return chain;
	}

private:
	Order order;
};
}
