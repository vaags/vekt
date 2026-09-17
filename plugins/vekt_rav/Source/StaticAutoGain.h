#pragma once

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <concepts>
#include <numbers>

namespace vekt::rav
{
template <std::floating_point Sample>
class StaticAutoGain final
{
public:
    void prepare(double sampleRate, double rampDurationSeconds = 0.02)
    {
        if (!calibrated)
        {
            calibrate();
            calibrated = true;
        }

        gain.reset(sampleRate, rampDurationSeconds);
        gain.setCurrentAndTargetValue(gain.getTargetValue());
    }

    void reset() noexcept
    {
        gain.setCurrentAndTargetValue(gain.getTargetValue());
    }

    void setParameters(Sample driveDecibels, Sample bias, int modeIndex, bool enabled) noexcept
    {
        const auto modeCompensation = modeCompensations[static_cast<std::size_t>(
            std::clamp(modeIndex, 0, static_cast<int>(modeCompensations.size() - 1)))];
        const auto compensatedGain = interpolatedGain(driveDecibels, bias)
            * modeCompensation * topologyCompensation;
        gain.setTargetValue(enabled ? std::min(compensatedGain, Sample { 1 }) : Sample { 1 });
    }

    void setParameters(Sample driveDecibels, Sample bias, bool enabled) noexcept
    {
        setParameters(driveDecibels, bias, 0, enabled);
    }

    void setTopologyCompensation(Sample compensation) noexcept
    {
        topologyCompensation = std::clamp(compensation, Sample { 0.25 }, Sample { 1.5 });
    }

    [[nodiscard]] Sample getTargetGain() const noexcept
    {
        return gain.getTargetValue();
    }

    void process(juce::dsp::AudioBlock<Sample> block) noexcept
    {
        for (std::size_t sample = 0; sample < block.getNumSamples(); ++sample)
        {
            const auto currentGain = gain.getNextValue();
            for (std::size_t channel = 0; channel < block.getNumChannels(); ++channel)
                block.getChannelPointer(channel)[sample] *= currentGain;
        }
    }

private:
    static constexpr std::size_t drivePointCount = 13;
    static constexpr std::size_t biasPointCount = 9;
    static constexpr Sample driveStepDb = static_cast<Sample>(3);
    static constexpr Sample biasStep = static_cast<Sample>(0.125);
    static constexpr Sample minimumGain = static_cast<Sample>(0.06309573444801933);
    static constexpr double referencePeak = 0.12589254117941673;
    static constexpr int calibrationSamples = 2'048;
    inline static constexpr std::array modeCompensations {
        static_cast<Sample>(1.0), static_cast<Sample>(0.92), static_cast<Sample>(0.86),
        static_cast<Sample>(0.55), static_cast<Sample>(0.90), static_cast<Sample>(0.78) };

    void calibrate() noexcept
    {
        for (std::size_t driveIndex = 0; driveIndex < drivePointCount; ++driveIndex)
        {
            const auto driveDb = static_cast<double>(driveIndex) * static_cast<double>(driveStepDb);
            for (std::size_t biasIndex = 0; biasIndex < biasPointCount; ++biasIndex)
            {
                const auto bias = static_cast<double>(biasIndex) * static_cast<double>(biasStep);
                calibration[index(driveIndex, biasIndex)] = static_cast<Sample>(calibratedGain(driveDb, bias));
            }
        }
    }

    [[nodiscard]] static double calibratedGain(double driveDb, double bias) noexcept
    {
        const auto drive = std::pow(10.0, driveDb / 20.0);
        auto sum = 0.0;
        auto sumOfSquares = 0.0;

        for (auto sample = 0; sample < calibrationSamples; ++sample)
        {
            const auto phase = 2.0 * std::numbers::pi * static_cast<double>(sample)
                / static_cast<double>(calibrationSamples);
            const auto input = referencePeak * std::sin(phase);
            const auto output = std::tanh((drive * input) + bias) - std::tanh(bias);
            sum += output;
            sumOfSquares += output * output;
        }

        const auto mean = sum / static_cast<double>(calibrationSamples);
        const auto variance = std::max(
            0.0, (sumOfSquares / static_cast<double>(calibrationSamples)) - (mean * mean));
        const auto outputRms = std::sqrt(variance);
        const auto referenceRms = referencePeak / std::numbers::sqrt2;
        return std::clamp(referenceRms / outputRms, static_cast<double>(minimumGain), 1.0);
    }

    [[nodiscard]] Sample interpolatedGain(Sample driveDb, Sample bias) const noexcept
    {
        const auto drivePosition = std::clamp(driveDb, Sample {}, static_cast<Sample>(36)) / driveStepDb;
        const auto biasPosition = std::abs(std::clamp(bias, Sample { -1 }, Sample { 1 })) / biasStep;
        const auto lowerDrive = static_cast<std::size_t>(std::floor(drivePosition));
        const auto lowerBias = static_cast<std::size_t>(std::floor(biasPosition));
        const auto upperDrive = std::min(lowerDrive + 1, drivePointCount - 1);
        const auto upperBias = std::min(lowerBias + 1, biasPointCount - 1);
        const auto driveFraction = drivePosition - static_cast<Sample>(lowerDrive);
        const auto biasFraction = biasPosition - static_cast<Sample>(lowerBias);

        const auto lower = std::lerp(
            calibration[index(lowerDrive, lowerBias)],
            calibration[index(upperDrive, lowerBias)],
            driveFraction);
        const auto upper = std::lerp(
            calibration[index(lowerDrive, upperBias)],
            calibration[index(upperDrive, upperBias)],
            driveFraction);
        return std::lerp(lower, upper, biasFraction);
    }

    [[nodiscard]] static constexpr std::size_t index(
        std::size_t driveIndex, std::size_t biasIndex) noexcept
    {
        return (driveIndex * biasPointCount) + biasIndex;
    }

    std::array<Sample, drivePointCount * biasPointCount> calibration {};
    juce::SmoothedValue<Sample, juce::ValueSmoothingTypes::Linear> gain { Sample { 1 } };
    Sample topologyCompensation { 1 };
    bool calibrated {};
};
}
