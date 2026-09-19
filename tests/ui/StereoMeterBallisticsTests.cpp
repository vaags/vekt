#include <vekt/ui/StereoMeterBallistics.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>

namespace
{
using Ballistics = vekt::ui::StereoMeterBallistics;
using Clock = Ballistics::Clock;

Clock::time_point at(std::chrono::milliseconds elapsed)
{
	return Clock::time_point {} + elapsed;
}
}

TEST_CASE("StereoMeterBallistics tracks channels independently", "[ui][meter]")
{
	Ballistics meter;
	const auto levels = meter.update({ 0.25f, 0.75f }, at(std::chrono::milliseconds { 0 }));
	REQUIRE(levels[0] == Catch::Approx(0.25f));
	REQUIRE(levels[1] == Catch::Approx(0.75f));
}

TEST_CASE("StereoMeterBallistics uses elapsed time for release", "[ui][meter]")
{
	Ballistics oneUpdate;
	Ballistics twoUpdates;
	oneUpdate.update({ 1.0f, 1.0f }, at(std::chrono::milliseconds { 0 }));
	twoUpdates.update({ 1.0f, 1.0f }, at(std::chrono::milliseconds { 0 }));

	const auto afterOneUpdate = oneUpdate.update({ 0.0f, 0.0f }, at(std::chrono::milliseconds { 100 }));
	twoUpdates.update({ 0.0f, 0.0f }, at(std::chrono::milliseconds { 40 }));
	const auto afterTwoUpdates = twoUpdates.update({ 0.0f, 0.0f }, at(std::chrono::milliseconds { 100 }));

	REQUIRE(afterOneUpdate[0] == Catch::Approx(afterTwoUpdates[0]));
	REQUIRE(afterOneUpdate[1] == Catch::Approx(afterTwoUpdates[1]));
}

TEST_CASE("StereoMeterBallistics sanitises invalid values and clears silent lanes", "[ui][meter]")
{
	Ballistics meter;
	const auto sanitised = meter.update({ -0.5f, std::numeric_limits<float>::infinity() },
		at(std::chrono::milliseconds { 0 }));
	REQUIRE(sanitised == Ballistics::Levels { 0.0f, 0.0f });

	meter.update({ 0.5f, 0.0f }, at(std::chrono::milliseconds { 1 }));
	const auto cleared = meter.update({ 0.0f, 0.0f }, at(std::chrono::seconds { 3 }));
	REQUIRE(cleared == Ballistics::Levels { 0.0f, 0.0f });
}
