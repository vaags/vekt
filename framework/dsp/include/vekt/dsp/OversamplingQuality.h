#pragma once

#include <cstddef>

namespace vekt::dsp
{
enum class OversamplingFactor
{
    off,
    x2,
    x4,
    x8,
    x16
};

enum class OversamplingFilter
{
    polyphaseIIR,
    polyphaseFIR
};

struct OversamplingQuality
{
    OversamplingFactor factor { OversamplingFactor::x4 };
    OversamplingFilter filter{OversamplingFilter::polyphaseIIR};

    [[nodiscard]] constexpr std::size_t multiplier() const noexcept
    {
        switch (factor)
        {
            case OversamplingFactor::off: return 1;
            case OversamplingFactor::x2:  return 2;
            case OversamplingFactor::x4:  return 4;
            case OversamplingFactor::x8:
                return 8;
            case OversamplingFactor::x16:
                return 16;
            }

        return 1;
    }

    friend constexpr bool operator== (const OversamplingQuality&, const OversamplingQuality&) = default;
};
}
