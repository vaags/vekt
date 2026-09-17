#include <vekt/dsp/MatchedToneStage.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace
{
constexpr auto sampleRate = 48'000.0;

double measuredGain(double frequency, double slope)
{
    constexpr auto sampleCount = 8'192;
    constexpr auto settlingSamples = 2'048;
    juce::AudioBuffer<double> buffer(1, sampleCount);
    for (auto sample = 0; sample < sampleCount; ++sample)
        buffer.setSample(0, sample, std::sin(2.0 * std::numbers::pi * frequency * sample / sampleRate));

    vekt::dsp::MatchedToneStage<double> tone;
    tone.prepare(sampleRate, 1, 1'000.0, 0.0);
    tone.setSlopeDbPerOctave(slope);
    tone.processPre(juce::dsp::AudioBlock<double>(buffer));

    auto sumOfSquares = 0.0;
    for (auto sample = settlingSamples; sample < sampleCount; ++sample)
    {
        const auto output = buffer.getSample(0, sample);
        sumOfSquares += output * output;
    }

    return std::sqrt(sumOfSquares / static_cast<double>(sampleCount - settlingSamples));
}
}

TEST_CASE("MatchedToneStage is transparent at neutral tone", "[dsp][tone]")
{
    juce::AudioBuffer<double> buffer(2, 256);
    for (auto channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (auto sample = 0; sample < buffer.getNumSamples(); ++sample)
            buffer.setSample(channel, sample, std::sin(static_cast<double>(sample) * 0.17));
    const juce::AudioBuffer<double> reference(buffer);

    vekt::dsp::MatchedToneStage<double> tone;
    tone.prepare(sampleRate, 2);
    tone.processPre(juce::dsp::AudioBlock<double>(buffer));
    tone.processPost(juce::dsp::AudioBlock<double>(buffer));

    for (auto channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (auto sample = 0; sample < buffer.getNumSamples(); ++sample)
            REQUIRE(buffer.getSample(channel, sample)
                == Catch::Approx(reference.getSample(channel, sample)).margin(1.0e-12));
}

TEST_CASE("MatchedToneStage pre and post filters cancel in a linear path", "[dsp][tone]")
{
    juce::AudioBuffer<double> buffer(2, 2'048);
    buffer.clear();
    buffer.setSample(0, 0, 1.0);
    buffer.setSample(1, 0, -1.0);

    vekt::dsp::MatchedToneStage<double> tone;
    tone.prepare(sampleRate, 2, 1'000.0, 0.0);
    tone.setSlopeDbPerOctave(4.0);
    tone.processPre(juce::dsp::AudioBlock<double>(buffer));
    tone.processPost(juce::dsp::AudioBlock<double>(buffer));

    REQUIRE(buffer.getSample(0, 0) == Catch::Approx(1.0).margin(1.0e-12));
    REQUIRE(buffer.getSample(1, 0) == Catch::Approx(-1.0).margin(1.0e-12));
    for (auto sample = 1; sample < buffer.getNumSamples(); ++sample)
    {
        REQUIRE(std::abs(buffer.getSample(0, sample)) < 1.0e-12);
        REQUIRE(std::abs(buffer.getSample(1, sample)) < 1.0e-12);
    }
}

TEST_CASE("MatchedToneStage slope controls spectral direction", "[dsp][tone]")
{
    REQUIRE(measuredGain(4'000.0, 3.0) > measuredGain(250.0, 3.0));
    REQUIRE(measuredGain(4'000.0, -3.0) < measuredGain(250.0, -3.0));
}

TEST_CASE("MatchedToneStage keeps its one kilohertz pivot at unity", "[dsp][tone]")
{
    constexpr auto sineRms = 0.7071067811865476;

    REQUIRE(measuredGain(1'000.0, 6.0) == Catch::Approx(sineRms).epsilon(0.001));
    REQUIRE(measuredGain(1'000.0, -6.0) == Catch::Approx(sineRms).epsilon(0.001));
}
