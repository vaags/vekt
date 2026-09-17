#pragma once

#include <algorithm>
#include <cmath>
#include <concepts>
#include <span>

#include <juce_audio_basics/juce_audio_basics.h>

namespace vekt::dsp
{
template <std::floating_point Sample>
class TanhStage final
{
public:
    void prepare(double sampleRate, double rampDurationSeconds = 0.02) noexcept
    {
        drive.reset(sampleRate, rampDurationSeconds);
        bias.reset(sampleRate, rampDurationSeconds);
    }

    void reset() noexcept
    {
        drive.setCurrentAndTargetValue(drive.getTargetValue());
        bias.setCurrentAndTargetValue(bias.getTargetValue());
    }

    void setDriveLinear(Sample newDrive) noexcept
    {
        drive.setTargetValue(std::max(newDrive, Sample {}));
    }

    void setBias(Sample newBias) noexcept
    {
        bias.setTargetValue(std::clamp(newBias, Sample { -1 }, Sample { 1 }));
    }

    [[nodiscard]] Sample processSample(Sample input) noexcept
    {
        const auto smoothedDrive = drive.getNextValue();
        const auto smoothedBias = bias.getNextValue();
        return std::tanh((smoothedDrive * input) + smoothedBias) - std::tanh(smoothedBias);
    }

    void process(std::span<Sample> samples) noexcept
    {
        for (auto& sample : samples)
            sample = processSample(sample);
    }

private:
    juce::SmoothedValue<Sample, juce::ValueSmoothingTypes::Linear> drive { Sample { 1 } };
    juce::SmoothedValue<Sample, juce::ValueSmoothingTypes::Linear> bias;
};
}
