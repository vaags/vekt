#include <vekt/dsp/OversamplingBank.h>

#include <catch2/catch_test_macros.hpp>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <cmath>

namespace
{
constexpr std::array qualities {
    vekt::dsp::OversamplingQuality { vekt::dsp::OversamplingFactor::off, vekt::dsp::OversamplingPhase::minimum },
    vekt::dsp::OversamplingQuality { vekt::dsp::OversamplingFactor::off, vekt::dsp::OversamplingPhase::linear },
    vekt::dsp::OversamplingQuality { vekt::dsp::OversamplingFactor::x2, vekt::dsp::OversamplingPhase::minimum },
    vekt::dsp::OversamplingQuality { vekt::dsp::OversamplingFactor::x2, vekt::dsp::OversamplingPhase::linear },
    vekt::dsp::OversamplingQuality { vekt::dsp::OversamplingFactor::x4, vekt::dsp::OversamplingPhase::minimum },
    vekt::dsp::OversamplingQuality { vekt::dsp::OversamplingFactor::x4, vekt::dsp::OversamplingPhase::linear }
};
}

TEST_CASE("OversamplingBank exposes every configured quality path", "[dsp][oversampling]")
{
    constexpr auto channelCount = 2;
    constexpr auto blockSize = 64;

    vekt::dsp::OversamplingBank<float> bank(channelCount);
    bank.prepare(blockSize);

    REQUIRE(bank.getMaximumLatencySamples() > 0);

    for (const auto quality : qualities)
    {
        bank.activate(quality);

        REQUIRE(bank.getActiveQuality() == quality);
        REQUIRE(bank.getActiveFactor() == quality.multiplier());
        REQUIRE(bank.getActiveLatencySamples() >= 0);
        REQUIRE(bank.getActiveLatencySamples() <= bank.getMaximumLatencySamples());

        if (quality.factor == vekt::dsp::OversamplingFactor::off)
            REQUIRE(bank.getActiveLatencySamples() == 0);
    }
}

TEST_CASE("OversamplingBank processes finite stereo blocks without resizing", "[dsp][oversampling]")
{
    constexpr auto channelCount = 2;
    constexpr auto blockSize = 64;

    vekt::dsp::OversamplingBank<float> bank(channelCount);
    juce::AudioBuffer<float> buffer(channelCount, blockSize);
    bank.prepare(blockSize);

    for (const auto quality : qualities)
    {
        buffer.clear();
        buffer.setSample(0, 0, 1.0f);
        buffer.setSample(1, 0, -1.0f);
        bank.activate(quality);

        juce::dsp::AudioBlock<float> output(buffer);
        const juce::dsp::AudioBlock<const float> input(output);
        auto oversampled = bank.processSamplesUp(input);

        REQUIRE(oversampled.getNumChannels() == channelCount);
        REQUIRE(oversampled.getNumSamples() == blockSize * quality.multiplier());

        for (std::size_t channel = 0; channel < oversampled.getNumChannels(); ++channel)
            for (std::size_t sample = 0; sample < oversampled.getNumSamples(); ++sample)
                oversampled.setSample(static_cast<int>(channel), static_cast<int>(sample),
                                      std::tanh(oversampled.getSample(static_cast<int>(channel),
                                                                     static_cast<int>(sample))));

        bank.processSamplesDown(output);

        for (auto channel = 0; channel < buffer.getNumChannels(); ++channel)
            for (auto sample = 0; sample < buffer.getNumSamples(); ++sample)
                REQUIRE(std::isfinite(buffer.getSample(channel, sample)));
    }
}
