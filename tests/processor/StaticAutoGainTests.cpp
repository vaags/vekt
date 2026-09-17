#include <StaticAutoGain.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
#include <numbers>

namespace
{
double referenceGain(double driveDb, double bias)
{
    constexpr auto sampleCount = 2'048;
    constexpr auto referencePeak = 0.12589254117941673;
    const auto drive = std::pow(10.0, driveDb / 20.0);
    auto sum = 0.0;
    auto sumOfSquares = 0.0;

    for (auto sample = 0; sample < sampleCount; ++sample)
    {
        const auto phase = 2.0 * std::numbers::pi * static_cast<double>(sample)
            / static_cast<double>(sampleCount);
        const auto output = std::tanh((drive * referencePeak * std::sin(phase)) + bias)
            - std::tanh(bias);
        sum += output;
        sumOfSquares += output * output;
    }

    const auto mean = sum / static_cast<double>(sampleCount);
    const auto outputRms = std::sqrt(
        (sumOfSquares / static_cast<double>(sampleCount)) - (mean * mean));
    return std::clamp((referencePeak / std::numbers::sqrt2) / outputRms, 0.06309573444801933, 1.0);
}
}

TEST_CASE("StaticAutoGain matches its reference calibration", "[processor][auto-gain]")
{
    vekt::rav::StaticAutoGain<double> autoGain;
    autoGain.prepare(48'000.0, 0.0);
    autoGain.setParameters(18.0, 0.5, true);

    REQUIRE(autoGain.getTargetGain() == Catch::Approx(referenceGain(18.0, 0.5)).epsilon(1.0e-12));
}

TEST_CASE("StaticAutoGain is symmetric in bias and never boosts", "[processor][auto-gain]")
{
    vekt::rav::StaticAutoGain<double> autoGain;
    autoGain.prepare(48'000.0, 0.0);
    autoGain.setParameters(12.0, 0.75, true);
    const auto positiveBiasGain = autoGain.getTargetGain();
    autoGain.setParameters(12.0, -0.75, true);

    REQUIRE(autoGain.getTargetGain() == Catch::Approx(positiveBiasGain));
    REQUIRE(autoGain.getTargetGain() <= 1.0);
}

TEST_CASE("StaticAutoGain bypasses compensation when disabled", "[processor][auto-gain]")
{
    vekt::rav::StaticAutoGain<double> autoGain;
    autoGain.prepare(48'000.0, 0.0);
    autoGain.setParameters(36.0, 0.0, false);

    REQUIRE(autoGain.getTargetGain() == Catch::Approx(1.0));
}

TEST_CASE("StaticAutoGain applies calibrated gain to every channel", "[processor][auto-gain]")
{
    juce::AudioBuffer<double> buffer(2, 32);
    for (auto channel = 0; channel < buffer.getNumChannels(); ++channel)
        juce::FloatVectorOperations::fill(buffer.getWritePointer(channel), 1.0, buffer.getNumSamples());

    vekt::rav::StaticAutoGain<double> autoGain;
    autoGain.prepare(48'000.0, 0.0);
    autoGain.setParameters(18.0, 0.5, true);
    const auto expected = autoGain.getTargetGain();
    autoGain.process(juce::dsp::AudioBlock<double>(buffer));

    for (auto channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (auto sample = 0; sample < buffer.getNumSamples(); ++sample)
            REQUIRE(buffer.getSample(channel, sample) == Catch::Approx(expected));
}
