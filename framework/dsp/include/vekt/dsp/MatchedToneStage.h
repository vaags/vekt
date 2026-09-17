#pragma once

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <concepts>
#include <numbers>
#include <vector>

namespace vekt::dsp
{
template <std::floating_point Sample>
class MatchedToneStage final
{
public:
    void prepare(
        double sampleRate,
        std::size_t channelCount,
        Sample pivotHz = static_cast<Sample>(1'000),
        double rampDurationSeconds = 0.02)
    {
        sampleRateHz = static_cast<Sample>(sampleRate);
        const auto boundedPivot = std::clamp(
            pivotHz, static_cast<Sample>(1), sampleRateHz * static_cast<Sample>(0.45));
        const auto pivotRadians = std::numbers::pi_v<Sample> * boundedPivot / sampleRateHz;
        warpedPivot = static_cast<Sample>(2) * sampleRateHz * std::tan(pivotRadians);
        warpSlope = static_cast<Sample>(2) * pivotRadians / std::sin(static_cast<Sample>(2) * pivotRadians);

        const auto currentSlope = preSlope.getTargetValue();
        preSlope.reset(sampleRate, rampDurationSeconds);
        postSlope.reset(sampleRate, rampDurationSeconds);
        preSlope.setCurrentAndTargetValue(currentSlope);
        postSlope.setCurrentAndTargetValue(currentSlope);
        preStates.assign(channelCount, {});
        postStates.assign(channelCount, {});
    }

    void reset() noexcept
    {
        preSlope.setCurrentAndTargetValue(preSlope.getTargetValue());
        postSlope.setCurrentAndTargetValue(postSlope.getTargetValue());
        std::fill(preStates.begin(), preStates.end(), State {});
        std::fill(postStates.begin(), postStates.end(), State {});
    }

    void setSlopeDbPerOctave(Sample newSlope) noexcept
    {
        const auto slope = std::clamp(newSlope, static_cast<Sample>(-6), static_cast<Sample>(6));
        preSlope.setTargetValue(slope);
        postSlope.setTargetValue(slope);
    }

    void processPre(juce::dsp::AudioBlock<Sample> block) noexcept
    {
        process(block, preStates, preSlope, false);
    }

    void processPost(juce::dsp::AudioBlock<Sample> block) noexcept
    {
        process(block, postStates, postSlope, true);
    }

private:
    struct Coefficients
    {
        Sample b0;
        Sample b1;
        Sample a1;
    };

    struct State
    {
        Sample previousInput {};
        Sample previousOutput {};
    };

    [[nodiscard]] Coefficients coefficientsFor(Sample slopeDbPerOctave) const noexcept
    {
        constexpr auto firstOrderSlope = static_cast<Sample>(20)
            * std::numbers::ln2_v<Sample> / std::numbers::ln10_v<Sample>;
        const auto normalizedSlope = std::clamp(
            slopeDbPerOctave / (firstOrderSlope * warpSlope),
            static_cast<Sample>(-0.999),
            static_cast<Sample>(0.999));
        const auto shelfRatio = (static_cast<Sample>(1) - normalizedSlope)
            / (static_cast<Sample>(1) + normalizedSlope);
        const auto rootRatio = std::sqrt(shelfRatio);
        const auto highGain = static_cast<Sample>(1) / rootRatio;
        const auto zeroFrequency = warpedPivot * rootRatio;
        const auto poleFrequency = warpedPivot / rootRatio;
        const auto bilinearScale = static_cast<Sample>(2) * sampleRateHz;
        const auto denominator = bilinearScale + poleFrequency;

        return {
            highGain * (bilinearScale + zeroFrequency) / denominator,
            highGain * (zeroFrequency - bilinearScale) / denominator,
            (poleFrequency - bilinearScale) / denominator
        };
    }

    void process(
        juce::dsp::AudioBlock<Sample> block,
        std::vector<State>& states,
        juce::SmoothedValue<Sample, juce::ValueSmoothingTypes::Linear>& slope,
        bool inverse) noexcept
    {
        jassert(block.getNumChannels() <= states.size());

        for (std::size_t sample = 0; sample < block.getNumSamples(); ++sample)
        {
            const auto currentSlope = slope.getNextValue() * (inverse ? Sample { -1 } : Sample { 1 });
            const auto coefficients = coefficientsFor(currentSlope);

            for (std::size_t channel = 0; channel < block.getNumChannels(); ++channel)
            {
                auto* samples = block.getChannelPointer(channel);
                auto& state = states[channel];
                const auto input = samples[sample];
                const auto output = (coefficients.b0 * input)
                    + (coefficients.b1 * state.previousInput)
                    - (coefficients.a1 * state.previousOutput);
                state.previousInput = input;
                state.previousOutput = output;
                samples[sample] = output;
            }
        }
    }

    std::vector<State> preStates;
    std::vector<State> postStates;
    juce::SmoothedValue<Sample, juce::ValueSmoothingTypes::Linear> preSlope;
    juce::SmoothedValue<Sample, juce::ValueSmoothingTypes::Linear> postSlope;
    Sample sampleRateHz { static_cast<Sample>(48'000) };
    Sample warpedPivot {};
    Sample warpSlope { 1 };
};
}
