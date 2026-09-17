#include <vekt/dsp/TanhStage.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>

TEST_CASE("TanhStage is centred at silence for every bias", "[dsp][tanh]")
{
    vekt::dsp::TanhStage<double> stage;

    for (const auto bias : { -1.0, -0.5, 0.0, 0.5, 1.0 })
    {
        stage.setBias(bias);
        REQUIRE(stage.processSample(0.0) == Catch::Approx(0.0).margin(1.0e-12));
    }
}

TEST_CASE("TanhStage transfer is finite and strictly increasing", "[dsp][tanh]")
{
    vekt::dsp::TanhStage<double> stage;
    stage.setDriveLinear(8.0);
    stage.setBias(0.35);

    auto previous = stage.processSample(-2.0);
    REQUIRE(std::isfinite(previous));

    for (auto index = 1; index <= 400; ++index)
    {
        const auto input = -2.0 + (static_cast<double>(index) * 0.01);
        const auto output = stage.processSample(input);

        REQUIRE(std::isfinite(output));
        REQUIRE(output > previous);
        previous = output;
    }
}

TEST_CASE("TanhStage processes buffers in place", "[dsp][tanh]")
{
    vekt::dsp::TanhStage<float> stage;
    stage.setDriveLinear(2.0f);

    std::array samples { -1.0f, -0.25f, 0.0f, 0.25f, 1.0f };
    stage.process(samples);

    REQUIRE(samples.front() == Catch::Approx(std::tanh(-2.0f)));
    REQUIRE(samples[2] == Catch::Approx(0.0f));
    REQUIRE(samples.back() == Catch::Approx(std::tanh(2.0f)));
}
