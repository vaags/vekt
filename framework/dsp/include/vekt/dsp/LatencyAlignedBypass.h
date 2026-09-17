#pragma once

#include <juce_dsp/juce_dsp.h>

#include <algorithm>

namespace vekt::dsp
{
template <typename Sample>
class LatencyAlignedBypass final
{
public:
    void prepare(const juce::dsp::ProcessSpec& specification, int maximumLatencySamples)
    {
        maximumLatency = std::max(0, maximumLatencySamples);
        delay.setMaximumDelayInSamples(maximumLatency);
        delay.prepare(specification);
        setLatency(latency);
    }

    void reset() noexcept
    {
        delay.reset();
    }

    void setLatency(int latencySamples) noexcept
    {
        latency = std::clamp(latencySamples, 0, maximumLatency);
        delay.setDelay(static_cast<Sample>(latency));
    }

    [[nodiscard]] int getLatency() const noexcept
    {
        return latency;
    }

    void advance(const juce::dsp::AudioBlock<const Sample>& input) noexcept
    {
        process(input, nullptr);
    }

    void processReplacing(juce::dsp::AudioBlock<Sample> block) noexcept
    {
        process(juce::dsp::AudioBlock<const Sample>(block), &block);
    }

private:
    void process(
        const juce::dsp::AudioBlock<const Sample>& input,
        juce::dsp::AudioBlock<Sample>* output) noexcept
    {
        for (std::size_t channel = 0; channel < input.getNumChannels(); ++channel)
        {
            const auto* inputSamples = input.getChannelPointer(channel);
            auto* outputSamples = output != nullptr ? output->getChannelPointer(channel) : nullptr;

            for (std::size_t sample = 0; sample < input.getNumSamples(); ++sample)
            {
                delay.pushSample(static_cast<int>(channel), inputSamples[sample]);
                const auto delayed = delay.popSample(static_cast<int>(channel));

                if (outputSamples != nullptr)
                    outputSamples[sample] = delayed;
            }
        }
    }

    juce::dsp::DelayLine<Sample, juce::dsp::DelayLineInterpolationTypes::None> delay;
    int maximumLatency {};
    int latency {};
};
}