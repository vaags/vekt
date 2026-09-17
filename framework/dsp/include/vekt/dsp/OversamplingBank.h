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
        : paths {
            std::make_unique<Oversampler>(channelCount),
            makePath(channelCount, 1, FilterType::filterHalfBandPolyphaseIIR),
            makePath(channelCount, 2, FilterType::filterHalfBandPolyphaseIIR),
            makePath(channelCount, 1, FilterType::filterHalfBandFIREquiripple),
            makePath(channelCount, 2, FilterType::filterHalfBandFIREquiripple)
        }
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

        const auto factorOffset = quality.factor == OversamplingFactor::x2 ? 0U : 1U;
        const auto phaseOffset = quality.phase == OversamplingPhase::minimum ? 1U : 3U;
        return phaseOffset + factorOffset;
    }

    [[nodiscard]] static int latencyOf(const Oversampler& path) noexcept
    {
        return static_cast<int>(std::lround(path.getLatencyInSamples()));
    }

    std::array<std::unique_ptr<Oversampler>, 5> paths;
    OversamplingQuality activeQuality {};
    std::size_t activePathIndex { pathIndex(activeQuality) };
    int maximumLatencySamples {};
};
}
