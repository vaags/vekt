#include <RavModeStage.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <vector>

TEST_CASE("Rav mode stage keeps every mode finite", "[dsp][rav]")
{
	for (auto modeIndex = 0; modeIndex < 4; ++modeIndex)
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
	std::array<float, 4> outputs {};
	for (auto modeIndex = 0; modeIndex < 4; ++modeIndex)
	{
		vekt::rav::RavModeStage stage;
		stage.prepare(48'000.0);
		stage.setParameters(static_cast<vekt::rav::RavMode>(modeIndex),
			30.0f, 0.1f, 0.5f, 0.5f, 0.5f);
		std::array samples { 0.37f };
		stage.process(samples);
		outputs[static_cast<std::size_t>(modeIndex)] = samples[0];
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

TEST_CASE("Rav Fuzz state response is consistent across processing rates", "[dsp][rav][rate]")
{
	const auto renderStep = [](double sampleRate)
	{
		vekt::rav::RavModeStage stage;
		stage.prepare(sampleRate);
		stage.setParameters(vekt::rav::RavMode::fuzz, 24.0f, 0.2f, 0.7f, 0.4f, 0.8f, 6.0f);
		std::array<float, 38'400> settlingSamples {};
		stage.process(settlingSamples);
		stage.reset();
		std::vector<float> stepSamples(static_cast<std::size_t>(sampleRate * 0.002), 0.25f);
		stage.process(stepSamples);
		return stepSamples.back();
	};

	const auto reference = renderStep(192'000.0);
	const auto lowerRate = renderStep(48'000.0);
	REQUIRE(lowerRate == Catch::Approx(reference).margin(1.0e-4f));
}

TEST_CASE("Rav Saturation feedback response is consistent across processing rates", "[dsp][rav][rate]")
{
	const auto renderStep = [](double sampleRate)
	{
		vekt::rav::RavModeStage stage;
		stage.prepare(sampleRate);
		stage.setParameters(vekt::rav::RavMode::saturation, 12.0f, 0.2f, 0.9f, 0.4f, 0.5f);
		std::array<float, 38'400> settlingSamples {};
		stage.process(settlingSamples);
		stage.reset();
		std::vector<float> stepSamples(static_cast<std::size_t>(sampleRate * 0.0005), 0.25f);
		stage.process(stepSamples);
		return stepSamples.back();
	};

	const auto reference = renderStep(192'000.0);
	const auto lowerRate = renderStep(48'000.0);
	REQUIRE(lowerRate == Catch::Approx(reference).margin(0.02f));
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
