#pragma once

#include <algorithm>
#include <cmath>
#include <concepts>
#include <span>

namespace vekt::dsp
{
template <std::floating_point Sample>
class TanhStage final
{
public:
    void setDriveLinear(Sample newDrive) noexcept
    {
        drive = std::max(newDrive, Sample {});
    }

    void setBias(Sample newBias) noexcept
    {
        bias = std::clamp(newBias, Sample { -1 }, Sample { 1 });
        centreOffset = std::tanh(bias);
    }

    [[nodiscard]] Sample processSample(Sample input) const noexcept
    {
        return std::tanh((drive * input) + bias) - centreOffset;
    }

    void process(std::span<Sample> samples) const noexcept
    {
        for (auto& sample : samples)
            sample = processSample(sample);
    }

private:
    Sample drive { 1 };
    Sample bias {};
    Sample centreOffset {};
};
}
