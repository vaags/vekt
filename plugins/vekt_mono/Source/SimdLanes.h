#pragma once

#include <array>
#include <cmath>
#include <cstddef>

#if defined(__APPLE__)
#include <simd/simd.h>
#endif

namespace vekt::mono
{
template <std::size_t Lanes>
using SimdLaneValues = std::array<double, Lanes>;

// tanh of every lane at once. Apple's vector tanh is about 2.4x faster than scalar libm and within ~2 ulp of it
// (the same as the ladder's batched solve); elsewhere, and for any other lane count, scalar tanh per lane.
template <std::size_t Lanes>
[[nodiscard]] inline SimdLaneValues<Lanes> tanhSimdLanes(const SimdLaneValues<Lanes>& values) noexcept
{
#if defined(__APPLE__)
	if constexpr (Lanes == 2)
	{
		const auto result = simd::tanh(simd_double2 { values[0], values[1] });
		return { result.x, result.y };
	}
	else if constexpr (Lanes == 4)
	{
		const auto result = simd::tanh(simd_double4 { values[0], values[1], values[2], values[3] });
		return { result.x, result.y, result.z, result.w };
	}
#endif
	SimdLaneValues<Lanes> result {};
	for (std::size_t lane = 0; lane < Lanes; ++lane) result[lane] = std::tanh(values[lane]);
	return result;
}
}
