#include <RavPostStage.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

TEST_CASE("RavPostStage bypasses filtering when cutoff is disabled", "[dsp][rav][post]")
{
	vekt::rav::RavPostStage stage;
	stage.prepare(48'000.0);
	REQUIRE(stage.process(0.75f, 0.0f) == Catch::Approx(0.75f));
}

TEST_CASE("RavPostStage remains finite and resettable", "[dsp][rav][post]")
{
	vekt::rav::RavPostStage stage;
	stage.prepare(192'000.0);
	const auto first = stage.process(1.0f, 5'000.0f);
	REQUIRE(std::isfinite(first));
	stage.reset();
	REQUIRE(stage.process(1.0f, 5'000.0f) == Catch::Approx(first));
}
