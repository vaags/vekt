#include <RotorMotion.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("RotorMotion reaches the requested speed in the configured direction time", "[glimmer][rotor]")
{
	vekt::glimmer::RotorMotion rotor;
	rotor.prepare(100.0, 20.0f);
	rotor.setSpeeds(20.0f, 120.0f);
	rotor.setTransitionTimes(1.0f, 2.0f);
	rotor.setMode(vekt::glimmer::RotarySpeedMode::fast);

	for (int sample = 0; sample < 100; ++sample)
		rotor.advance();
	REQUIRE(rotor.getCurrentRpm() == Catch::Approx(120.0f));

	rotor.setMode(vekt::glimmer::RotarySpeedMode::slow);
	for (int sample = 0; sample < 100; ++sample)
		rotor.advance();
	REQUIRE(rotor.getCurrentRpm() == Catch::Approx(70.0f));

	for (int sample = 0; sample < 100; ++sample)
		rotor.advance();
	REQUIRE(rotor.getCurrentRpm() == Catch::Approx(20.0f));
}

TEST_CASE("RotorMotion changes targets without resetting phase", "[glimmer][rotor]")
{
	vekt::glimmer::RotorMotion rotor;
	rotor.prepare(100.0, 60.0f);
	rotor.setSpeeds(60.0f, 120.0f);
	rotor.setTransitionTimes(1.0f, 1.0f);

	rotor.advance();
	const auto phaseBeforeTransition = rotor.getPhaseTurns();
	rotor.setMode(vekt::glimmer::RotarySpeedMode::fast);
	const auto phaseAfterTransition = rotor.advance();

	REQUIRE(phaseAfterTransition > phaseBeforeTransition);
	REQUIRE(phaseAfterTransition < 1.0f);
}

TEST_CASE("RotorMotion uses the auto target without changing the selected mode", "[glimmer][rotor]")
{
	vekt::glimmer::RotorMotion rotor;
	rotor.prepare(100.0, 40.0f);
	rotor.setSpeeds(40.0f, 100.0f);
	rotor.setTransitionTimes(0.1f, 0.1f);
	rotor.setMode(vekt::glimmer::RotarySpeedMode::autoMode);
	rotor.setAutoFast(true);

	for (int sample = 0; sample < 10; ++sample)
		rotor.advance();
	REQUIRE(rotor.getCurrentRpm() == Catch::Approx(100.0f));
	REQUIRE(rotor.getMode() == vekt::glimmer::RotarySpeedMode::autoMode);
}

TEST_CASE("RotorMotion brake holds phase and overrides manual and Auto", "[glimmer][rotor]")
{
	using vekt::glimmer::RotarySpeedMode;
	vekt::glimmer::RotorMotion rotor;
	rotor.prepare(100.0, 60.0f);
	rotor.configure(60, 120, 1, 1, RotarySpeedMode::autoMode, true, true, 1, 1);
	rotor.setAutoFast(true);
	for (int sample = 0; sample < 100; ++sample) (void) rotor.advance();
	REQUIRE(rotor.getCurrentRpm() == Catch::Approx(0.0f));
	const auto stoppedPhase = rotor.getPhaseTurns();
	for (int sample = 0; sample < 100; ++sample) (void) rotor.advance();
	REQUIRE(rotor.getPhaseTurns() == Catch::Approx(stoppedPhase));
	rotor.configure(60, 120, 1, 1, RotarySpeedMode::autoMode, false, true, 0, 1);
	for (int sample = 0; sample < 100; ++sample) (void) rotor.advance();
	REQUIRE(rotor.getCurrentRpm() == Catch::Approx(60.0f));
	rotor.configure(60, 120, 1, 1, RotarySpeedMode::autoMode, false, false, 0, 1);
	for (int sample = 0; sample < 100; ++sample) (void) rotor.advance();
	REQUIRE(rotor.getCurrentRpm() == Catch::Approx(120.0f));
}

TEST_CASE("RotorMotion manual target changes use bounded slew and signed motion", "[glimmer][rotor]")
{
	vekt::glimmer::RotorMotion rotor;
	rotor.prepare(100.0, 20.0f);
	for (int sample = 0; sample < 100; ++sample)
	{
		const auto previous = rotor.getCurrentRpm();
		rotor.configure(20, 120, 1, 1, vekt::glimmer::RotarySpeedMode::slow,
			false, true, static_cast<float>(sample) / 99.0f, -1);
		(void) rotor.advance();
		REQUIRE(std::abs(rotor.getCurrentRpm() - previous) <= 1.001f);
	}
	for (int sample = 0; sample < 100; ++sample) (void) rotor.advance();
	REQUIRE(rotor.getCurrentRpm() == Catch::Approx(-120.0f));
}
