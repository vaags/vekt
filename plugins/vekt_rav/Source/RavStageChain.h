#pragma once

#include "RavModeStage.h"

#include <array>
#include <cstddef>

namespace vekt::rav
{
class RavStageChain final
{
public:
	inline static constexpr std::size_t stageCount = 6;

	using Order = std::array<RavMode, stageCount>;

	RavStageChain() noexcept
		: order { RavMode::saturation, RavMode::overdrive, RavMode::distortion,
			RavMode::fuzz, RavMode::wavefold, RavMode::bitcrush }
	{
	}

	[[nodiscard]] const Order& getOrder() const noexcept { return order; }

	void setOrder(Order candidate) noexcept
	{
		std::array<bool, stageCount> seen {};
		for (auto& mode : candidate)
		{
			const auto index = static_cast<std::size_t>(mode);
			if (index >= stageCount || seen[index])
				return;
			seen[index] = true;
		}
		order = candidate;
	}

private:
	Order order;
};
}
