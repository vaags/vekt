#include <vekt/dsp/StereoPeakMeter.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>

TEST_CASE("StereoPeakMeter accumulates finite stereo peaks", "[dsp][meter]")
{
	vekt::dsp::StereoPeakMeter meter;
	juce::AudioBuffer<float> first { 2, 3 };
	first.copyFrom(0, 0, std::array { -0.25f, 0.5f, -0.125f }.data(), 3);
	first.copyFrom(1, 0, std::array { 0.1f, -0.75f, 0.25f }.data(), 3);
	meter.publish(first);

	juce::AudioBuffer<float> second { 2, 2 };
	second.copyFrom(0, 0, std::array { 0.6f, 0.2f }.data(), 2);
	second.copyFrom(1, 0, std::array { 0.4f, 0.3f }.data(), 2);
	meter.publish(second);

	const auto peaks = meter.consumePeaks();
	REQUIRE(peaks[0] == Catch::Approx(0.6f));
	REQUIRE(peaks[1] == Catch::Approx(0.75f));
	REQUIRE(meter.consumePeaks() == std::array { 0.0f, 0.0f });
}

TEST_CASE("StereoPeakMeter ignores non-finite samples", "[dsp][meter]")
{
	vekt::dsp::StereoPeakMeter meter;
	juce::AudioBuffer<float> buffer { 2, 3 };
	buffer.copyFrom(0, 0, std::array {
		std::numeric_limits<float>::quiet_NaN(), 0.25f,
		std::numeric_limits<float>::infinity() }.data(), 3);
	buffer.copyFrom(1, 0, std::array { -0.5f, 0.0f, 0.25f }.data(), 3);
	meter.publish(buffer);

	const auto peaks = meter.consumePeaks();
	REQUIRE(peaks[0] == Catch::Approx(0.25f));
	REQUIRE(peaks[1] == Catch::Approx(0.5f));
}