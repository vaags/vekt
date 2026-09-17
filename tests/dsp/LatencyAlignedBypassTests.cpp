#include <vekt/dsp/LatencyAlignedBypass.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_audio_basics/juce_audio_basics.h>

TEST_CASE("LatencyAlignedBypass preserves samples at the configured latency", "[dsp][bypass]")
{
    constexpr auto blockSize = 16;
    constexpr auto latency = 5;
    juce::AudioBuffer<float> buffer(2, blockSize);
    buffer.clear();
    buffer.setSample(0, 0, 1.0f);
    buffer.setSample(1, 0, -1.0f);

    vekt::dsp::LatencyAlignedBypass<float> bypass;
    bypass.prepare({ 48'000.0, blockSize, 2 }, latency);
    bypass.setLatency(latency);
    bypass.processReplacing(juce::dsp::AudioBlock<float>(buffer));

    REQUIRE(buffer.getSample(0, latency) == Catch::Approx(1.0f));
    REQUIRE(buffer.getSample(1, latency) == Catch::Approx(-1.0f));
}

TEST_CASE("LatencyAlignedBypass remains primed across mode changes", "[dsp][bypass]")
{
    constexpr auto blockSize = 16;
    constexpr auto latency = 5;
    juce::AudioBuffer<float> buffer(2, blockSize);
    buffer.clear();
    buffer.setSample(0, blockSize - 1, 1.0f);

    vekt::dsp::LatencyAlignedBypass<float> bypass;
    bypass.prepare({ 48'000.0, blockSize, 2 }, latency);
    bypass.setLatency(latency);
    bypass.advance(juce::dsp::AudioBlock<const float>(buffer));

    buffer.clear();
    bypass.processReplacing(juce::dsp::AudioBlock<float>(buffer));

    REQUIRE(buffer.getSample(0, latency - 1) == Catch::Approx(1.0f));
}