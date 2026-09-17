#include <RavModeStage.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>

TEST_CASE("Rav mode stage keeps every mode finite", "[dsp][rav]")
{
	for (auto modeIndex = 0; modeIndex < 6; ++modeIndex)
	{
		vekt::rav::RavModeStage stage;
		stage.prepare(48'000.0);
		stage.setParameters(static_cast<vekt::rav::RavMode>(modeIndex),
			24.0f, 0.2f, 0.7f, 0.4f, 0.8f);
		std::array samples { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f };
		stage.process(samples);

		for (const auto sample : samples)
			REQUIRE(std::isfinite(sample));
	}
}

TEST_CASE("Rav mode stage reset is deterministic", "[dsp][rav]")
{
	vekt::rav::RavModeStage stage;
	stage.prepare(48'000.0);
	stage.setParameters(vekt::rav::RavMode::saturation, 18.0f, 0.3f, 0.8f, 0.2f, 0.6f);
	stage.reset();
	std::array first { 0.25f, -0.5f, 0.75f };
	stage.process(first);
	stage.reset();
	std::array second { 0.25f, -0.5f, 0.75f };
	stage.process(second);

	for (std::size_t index = 0; index < first.size(); ++index)
		REQUIRE(second[index] == Catch::Approx(first[index]).margin(1.0e-6f));
}

TEST_CASE("Rav mode stage modes produce different textures", "[dsp][rav]")
{
	std::array<float, 6> outputs {};
	for (auto modeIndex = 0; modeIndex < 6; ++modeIndex)
	{
		vekt::rav::RavModeStage stage;
		stage.prepare(48'000.0);
		stage.setParameters(static_cast<vekt::rav::RavMode>(modeIndex),
			30.0f, 0.1f, 0.5f, 0.5f, 0.5f);
		std::array samples { 0.37f };
		stage.process(samples);
		outputs[static_cast<std::size_t>(modeIndex)] = samples[0];
	}

	REQUIRE(std::abs(outputs[0] - outputs[4]) > 1.0e-3f);
	REQUIRE(std::abs(outputs[2] - outputs[5]) > 1.0e-3f);
}

TEST_CASE("Rav Bitcrush hold timing is base-rate invariant", "[dsp][rav][bitcrush]")
{
	vekt::rav::RavModeStage baseRateStage;
	vekt::rav::RavModeStage oversampledStage;
	baseRateStage.prepare(48'000.0, 48'000.0);
	oversampledStage.prepare(192'000.0, 48'000.0);
	baseRateStage.setParameters(vekt::rav::RavMode::bitcrush,
		0.0f, 0.0f, 0.5f, 0.5f, 0.0f);
	oversampledStage.setParameters(vekt::rav::RavMode::bitcrush,
		0.0f, 0.0f, 0.5f, 0.5f, 0.0f);
	baseRateStage.reset();
	oversampledStage.reset();

	for (auto baseSample = 0; baseSample < 64; ++baseSample)
	{
		const auto input = static_cast<float>(baseSample) / 64.0f;
		std::array baseBlock { input };
		baseRateStage.process(baseBlock);

		std::array oversampledBlock { input, input, input, input };
		oversampledStage.process(oversampledBlock);
		REQUIRE(oversampledBlock.back() == Catch::Approx(baseBlock.front()).margin(1.0e-6f));
	}
}

TEST_CASE("Rav mode stage supports slow Bias modulation", "[dsp][rav][bias]")
{
	vekt::rav::RavModeStage stage;
	stage.prepare(48'000.0);
	stage.setArtifactSafePolicy(true);
	stage.setParameters(vekt::rav::RavMode::fuzz, 24.0f, 1.0f, 0.7f, 0.4f, 0.8f);
	std::array samples { 0.25f, -0.25f, 0.25f, -0.25f };
	stage.process(samples);

	for (const auto sample : samples)
		REQUIRE(std::isfinite(sample));
}

TEST_CASE("Rav Distortion supports artifact-safe Bias transitions", "[dsp][rav][bias]")
{
	vekt::rav::RavModeStage stage;
	stage.prepare(48'000.0);
	stage.setArtifactSafePolicy(true);
	stage.setParameters(vekt::rav::RavMode::distortion, 24.0f, 1.0f, 0.7f, 0.4f, 0.8f);
	std::array samples { 0.25f, -0.25f, 0.25f, -0.25f };
	stage.process(samples);

	for (const auto sample : samples)
		REQUIRE(std::isfinite(sample));
}

TEST_CASE("Rav mode stage supports slow Texture modulation", "[dsp][rav][texture]")
{
	vekt::rav::RavModeStage stage;
	stage.prepare(48'000.0);
	stage.setArtifactSafePolicy(true);
	stage.setParameters(vekt::rav::RavMode::fuzz, 24.0f, 0.0f, 0.7f, 0.4f, 1.0f);
	std::array samples { 0.25f, -0.25f, 0.25f, -0.25f };
	stage.process(samples);

	for (const auto sample : samples)
		REQUIRE(std::isfinite(sample));
}

TEST_CASE("Rav Fuzz supports artifact-safe Drive transitions", "[dsp][rav][drive]")
{
	vekt::rav::RavModeStage stage;
	stage.prepare(48'000.0);
	stage.setArtifactSafePolicy(true);
	stage.setParameters(vekt::rav::RavMode::fuzz, 6.0f, 0.0f, 0.5f, 0.5f, 0.5f);
	std::array first { 0.2f, -0.2f, 0.2f, -0.2f };
	stage.process(first);
	stage.setParameters(vekt::rav::RavMode::fuzz, 36.0f, 0.0f, 0.5f, 0.5f, 0.5f);
	std::array second { 0.2f, -0.2f, 0.2f, -0.2f };
	stage.process(second);

	for (const auto sample : second)
		REQUIRE(std::isfinite(sample));
}
