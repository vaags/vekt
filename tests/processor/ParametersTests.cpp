#include <Parameters.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Rav Tone mapping preserves endpoints and softens extremes", "[processor][parameters]")
{
	REQUIRE(vekt::rav::parameters::toneSlopeFromUserValue(-6.0f)
		== Catch::Approx(-6.0f));
	REQUIRE(vekt::rav::parameters::toneSlopeFromUserValue(6.0f)
		== Catch::Approx(6.0f));
	REQUIRE(vekt::rav::parameters::toneSlopeFromUserValue(3.0f) > 3.0f);
	REQUIRE(vekt::rav::parameters::toneSlopeFromUserValue(-3.0f) < -3.0f);
}
