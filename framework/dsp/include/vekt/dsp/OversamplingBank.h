#pragma once

#include <vekt/dsp/OversamplingQuality.h>

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <memory>

namespace vekt::dsp
{
template <typename Sample>
class OversamplingBank final
{
public:
    explicit OversamplingBank(std::size_t channelCount)
        : paths{
              std::make_unique<Oversampler>(channelCount),
              makePath(channelCount, 1, FilterType::filterHalfBandPolyphaseIIR),
              makePath(channelCount, 2, FilterType::filterHalfBandPolyphaseIIR),
              makePath(channelCount, 1, FilterType::filterHalfBandFIREquiripple),
              makePath(channelCount, 2, FilterType::filterHalfBandFIREquiripple),
              makePath(channelCount, 3, FilterType::filterHalfBandFIREquiripple),
              makePath(channelCount, 4, FilterType::filterHalfBandFIREquiripple)}
    {
    }

    void prepare(std::size_t maximumBlockSize)
    {
        maximumLatencySamples = 0;

        for (auto& path : paths)
        {
            path->initProcessing(maximumBlockSize);
            maximumLatencySamples = std::max(maximumLatencySamples, latencyOf(*path));
        }
    }

    void reset() noexcept
    {
        for (auto& path : paths)
            path->reset();
    }

    // Call only while processing is suspended; changing paths resets filter state.
    void activate(OversamplingQuality newQuality) noexcept
    {
        activeQuality = newQuality;
        activePathIndex = pathIndex(newQuality);
        paths[activePathIndex]->reset();
    }

    [[nodiscard]] OversamplingQuality getActiveQuality() const noexcept
    {
        return activeQuality;
    }

    [[nodiscard]] std::size_t getActiveFactor() const noexcept
    {
        return paths[activePathIndex]->getOversamplingFactor();
    }

    [[nodiscard]] std::size_t getMaximumFactor() const noexcept
    {
        return 16;
    }

    [[nodiscard]] int getActiveLatencySamples() const noexcept
    {
        return latencyOf(*paths[activePathIndex]);
    }

    [[nodiscard]] int getMaximumLatencySamples() const noexcept
    {
        return maximumLatencySamples;
    }

    [[nodiscard]] juce::dsp::AudioBlock<Sample> processSamplesUp(
        const juce::dsp::AudioBlock<const Sample>& input) noexcept
    {
        return paths[activePathIndex]->processSamplesUp(input);
    }

    void processSamplesDown(juce::dsp::AudioBlock<Sample>& output) noexcept
    {
        paths[activePathIndex]->processSamplesDown(output);
    }

private:
    using Oversampler = juce::dsp::Oversampling<Sample>;
    using FilterType = typename Oversampler::FilterType;

    [[nodiscard]] static std::unique_ptr<Oversampler> makePath(
        std::size_t channelCount,
        std::size_t exponent,
        FilterType filterType)
    {
        return std::make_unique<Oversampler>(channelCount, exponent, filterType, true, true);
    }

    [[nodiscard]] static constexpr std::size_t pathIndex(OversamplingQuality quality) noexcept
    {
        if (quality.factor == OversamplingFactor::off)
            return 0;

        if (quality.filter == OversamplingFilter::polyphaseIIR)
            return quality.factor == OversamplingFactor::x2 ? 1U : 2U;

        switch (quality.factor)
        {
        case OversamplingFactor::x2:
            return 3U;
        case OversamplingFactor::x4:
            return 4U;
        case OversamplingFactor::x8:
            return 5U;
        case OversamplingFactor::x16:
            return 6U;
        case OversamplingFactor::off:
            break;
        }

        return 0;
    }

    [[nodiscard]] static int latencyOf(const Oversampler& path) noexcept
    {
        return static_cast<int>(std::lround(path.getLatencyInSamples()));
    }

    std::array<std::unique_ptr<Oversampler>, 7> paths;
    OversamplingQuality activeQuality {};
    std::size_t activePathIndex { pathIndex(activeQuality) };
    int maximumLatencySamples {};
};
}
