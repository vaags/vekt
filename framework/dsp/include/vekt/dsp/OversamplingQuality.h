#pragma once

#include <cstddef>

namespace vekt::dsp
{
enum class OversamplingFactor
{
    off,
    x2,
    x4
};

enum class OversamplingPhase
{
    minimum,
    linear
};

struct OversamplingQuality
{
    OversamplingFactor factor { OversamplingFactor::x4 };
    OversamplingPhase phase { OversamplingPhase::minimum };

    [[nodiscard]] constexpr std::size_t multiplier() const noexcept
    {
        switch (factor)
        {
            case OversamplingFactor::off: return 1;
            case OversamplingFactor::x2:  return 2;
            case OversamplingFactor::x4:  return 4;
        }

        return 1;
    }

    friend constexpr bool operator== (const OversamplingQuality&, const OversamplingQuality&) = default;
};
}
