#pragma once

#include <bit>
#include <cstdint>

namespace vekt::rav
{
// A filter coefficient recomputed only when the control it depends on changes. The formulas (expm1, log1p in double)
// are costly per sample at high internal rates, and the controls only move during a ramp; the result is the same.
struct RavCachedCoefficient
{
	template <typename Compute>
	[[nodiscard]] double get(float control, Compute&& compute) noexcept
	{
		if (const auto bits = std::bit_cast<std::uint32_t>(control); bits != key)
		{
			key = bits;
			value = compute();
		}
		return value;
	}
	void invalidate() noexcept { key = unset; }

private:
	static constexpr std::uint32_t unset = 0xffffffffu; // a NaN, which no control produces
	std::uint32_t key { unset };
	double value {};
};
}
