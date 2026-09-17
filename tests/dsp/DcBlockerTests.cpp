#include <vekt/dsp/DcBlocker.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

TEST_CASE("DcBlocker removes a constant signal", "[dsp][dc-blocker]")
{
    vekt::dsp::DcBlocker<double> blocker;
    blocker.prepare(48'000.0);

    auto output = 0.0;
    for (auto sample = 0; sample < 480'000; ++sample)
        output = blocker.processSample(0.75);

    REQUIRE(std::abs(output) < 1.0e-12);
}

TEST_CASE("DcBlocker reset clears its history", "[dsp][dc-blocker]")
{
    vekt::dsp::DcBlocker<double> blocker;
    blocker.prepare(48'000.0);
    REQUIRE(blocker.processSample(1.0) == Catch::Approx(1.0));
    blocker.reset();

    REQUIRE(blocker.processSample(0.0) == Catch::Approx(0.0).margin(1.0e-15));
}
