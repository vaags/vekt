#pragma once

#include <array>
#include <cmath>
#include <cstddef>

#if defined(__APPLE__)
#include <simd/simd.h>
#endif

namespace vekt::kobber
{
template <std::size_t Lanes>
using SimdLaneValues = std::array<double, Lanes>;

// tanh of every value at once. Apple's vector tanh is about 2.4x faster than scalar libm and within ~2 ulp of it.
// Every value goes through the vector kernel, four at a time with any remainder in a (padded) two- or four-wide
// call, also when there is only one: each element's result depends only on its own argument, not on its
// neighbours, its position or the vector width, so a filter lane renders the same bits however lanes are grouped
// (alone, in pairs or in fours; Multicore relies on this). Elsewhere, scalar tanh per value.
template <std::size_t Count>
[[nodiscard]] inline SimdLaneValues<Count> tanhSimdLanes(const SimdLaneValues<Count>& values) noexcept
{
	SimdLaneValues<Count> result {};
#if defined(__APPLE__)
	std::size_t index {};
	for (; index + 4 <= Count; index += 4)
	{
		const auto quad = simd::tanh(simd_double4 { values[index], values[index + 1], values[index + 2], values[index + 3] });
		for (std::size_t lane = 0; lane < 4; ++lane) result[index + lane] = quad[lane];
	}
	if constexpr (Count % 4 == 3)
	{
		const auto quad = simd::tanh(simd_double4 { values[index], values[index + 1], values[index + 2], values[index + 2] });
		for (std::size_t lane = 0; lane < 3; ++lane) result[index + lane] = quad[lane];
	}
	else if constexpr (Count % 4 == 2)
	{
		const auto pair = simd::tanh(simd_double2 { values[index], values[index + 1] });
		result[index] = pair[0];
		result[index + 1] = pair[1];
	}
	else if constexpr (Count % 4 == 1)
		result[index] = simd::tanh(simd_double2 { values[index], values[index] })[0];
#else
	for (std::size_t index = 0; index < Count; ++index) result[index] = std::tanh(values[index]);
#endif
	return result;
}
}
