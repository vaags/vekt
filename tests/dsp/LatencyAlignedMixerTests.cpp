#include <vekt/dsp/LatencyAlignedMixer.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

namespace
{
constexpr auto channelCount = 2;
constexpr auto blockSize = 64;

juce::dsp::ProcessSpec testSpec()
{
    return { 1000.0, blockSize, channelCount };
}

void fill(juce::AudioBuffer<float>& buffer, float value)
{
    for (auto channel = 0; channel < buffer.getNumChannels(); ++channel)
        juce::FloatVectorOperations::fill(buffer.getWritePointer(channel), value, buffer.getNumSamples());
}

void processBlock(vekt::dsp::LatencyAlignedMixer<float>& mixer,
                  juce::AudioBuffer<float>& dry,
                  juce::AudioBuffer<float>& wet)
{
    const juce::dsp::AudioBlock<const float> dryBlock(dry);
    juce::dsp::AudioBlock<float> wetBlock(wet);
    mixer.pushDrySamples(dryBlock);
    mixer.mixWetSamples(wetBlock);
}
}

TEST_CASE("LatencyAlignedMixer reaches exact linear endpoints", "[dsp][mix]")
{
    vekt::dsp::LatencyAlignedMixer<float> mixer;
    juce::AudioBuffer<float> dry(channelCount, blockSize);
    juce::AudioBuffer<float> wet(channelCount, blockSize);
    mixer.prepare(testSpec(), 16);

    fill(dry, 0.25f);
    fill(wet, 0.75f);
    mixer.setWetProportion(0.0f);
    processBlock(mixer, dry, wet);

    fill(wet, 0.75f);
    processBlock(mixer, dry, wet);
    REQUIRE(wet.getSample(0, blockSize - 1) == Catch::Approx(0.25f).margin(1.0e-6f));

    mixer.setWetProportion(1.0f);
    fill(wet, 0.75f);
    processBlock(mixer, dry, wet);

    fill(wet, 0.75f);
    processBlock(mixer, dry, wet);
    REQUIRE(wet.getSample(0, blockSize - 1) == Catch::Approx(0.75f).margin(1.0e-6f));
}

TEST_CASE("LatencyAlignedMixer delays dry samples by wet latency", "[dsp][mix]")
{
    constexpr auto latency = 7;

    vekt::dsp::LatencyAlignedMixer<float> mixer;
    juce::AudioBuffer<float> dry(channelCount, blockSize);
    juce::AudioBuffer<float> wet(channelCount, blockSize);
    mixer.prepare(testSpec(), 16);
    mixer.setWetProportion(0.0f);
    mixer.setWetLatency(latency);

    dry.clear();
    wet.clear();
    processBlock(mixer, dry, wet);

    dry.clear();
    dry.setSample(0, 0, 1.0f);
    wet.clear();
    processBlock(mixer, dry, wet);

    REQUIRE(mixer.getWetLatency() == latency);
    REQUIRE(wet.getSample(0, latency) == Catch::Approx(1.0f).margin(1.0e-6f));
}
