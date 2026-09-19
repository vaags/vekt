#include <AutoSpeedDetector.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("AutoSpeedDetector maps sensitivity to its documented threshold range", "[glimmer][auto]")
{
	vekt::glimmer::AutoSpeedDetector detector;
	detector.prepare(1'000.0);
	detector.setSensitivity(0.0f);
	REQUIRE(detector.getThresholdDb() == Catch::Approx(-6.0f));
	detector.setSensitivity(1.0f);
	REQUIRE(detector.getThresholdDb() == Catch::Approx(-48.0f));
}

TEST_CASE("AutoSpeedDetector uses the linked stereo envelope and hysteresis", "[glimmer][auto]")
{
	vekt::glimmer::AutoSpeedDetector detector;
	detector.prepare(1'000.0);
	detector.setSensitivity(0.0f);

	for (int sample = 0; sample < 100; ++sample)
		(void) detector.advance(0.0f, 1.0f);
	REQUIRE(detector.isFast());

	for (int sample = 0; sample < 200; ++sample)
		(void) detector.advance(0.0f, 0.3f);
	REQUIRE(detector.isFast());

	for (int sample = 0; sample < 1'000; ++sample)
		(void) detector.advance(0.0f, 0.0f);
	REQUIRE_FALSE(detector.isFast());
}

TEST_CASE("AutoSpeedDetector prevents immediate target chatter", "[glimmer][auto]")
{
	vekt::glimmer::AutoSpeedDetector detector;
	detector.prepare(1'000.0);
	detector.setSensitivity(0.0f);

	for (int sample = 0; sample < 100; ++sample)
		(void) detector.advance(1.0f, 0.0f);
	REQUIRE(detector.isFast());

	for (int sample = 0; sample < 199; ++sample)
		(void) detector.advance(0.0f, 0.0f);
	REQUIRE(detector.isFast());
}
