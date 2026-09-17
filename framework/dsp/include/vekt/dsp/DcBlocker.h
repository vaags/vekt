#pragma once

#include <algorithm>
#include <cmath>
#include <concepts>
#include <numbers>
#include <span>

namespace vekt::dsp
{
template <std::floating_point Sample>
class DcBlocker final
{
public:
    void prepare(double sampleRate, Sample cutoffHz = static_cast<Sample>(10)) noexcept
    {
        const auto maximumCutoff = static_cast<Sample>(sampleRate * 0.5);
        const auto cutoff = std::clamp(cutoffHz, Sample {}, maximumCutoff);
        coefficient = std::exp(
            static_cast<Sample>(-2) * std::numbers::pi_v<Sample> * cutoff
            / static_cast<Sample>(sampleRate));
        reset();
    }

    void reset() noexcept
    {
        previousInput = Sample {};
        previousOutput = Sample {};
    }

    [[nodiscard]] Sample processSample(Sample input) noexcept
    {
        const auto output = input - previousInput + (coefficient * previousOutput);
        previousInput = input;
        previousOutput = output;
        return output;
    }

    void process(std::span<Sample> samples) noexcept
    {
        for (auto& sample : samples)
            sample = processSample(sample);
    }

private:
    Sample coefficient {};
    Sample previousInput {};
    Sample previousOutput {};
};
}
