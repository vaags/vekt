#include <RavModeStage.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <vector>

TEST_CASE("Rav mode stage keeps every mode finite", "[dsp][rav]")
{
	for (auto modeIndex = 0; modeIndex < static_cast<int>(vekt::rav::ravModeCount); ++modeIndex)
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
	std::array<std::array<float, 128>, vekt::rav::ravModeCount> outputs {};
	for (auto modeIndex = 0; modeIndex < static_cast<int>(vekt::rav::ravModeCount); ++modeIndex)
	{
		vekt::rav::RavModeStage stage;
		stage.prepare(48'000.0);
		stage.setParameters(static_cast<vekt::rav::RavMode>(modeIndex),
			30.0f, 0.1f, 0.7f, 0.5f, 0.8f, 3.0f);
		auto& samples = outputs[static_cast<std::size_t>(modeIndex)];
		for (std::size_t sample = 0; sample < samples.size(); ++sample)
			samples[sample] = 0.37f * std::sin(static_cast<float>(sample) * 0.17f);
		stage.process(samples);
	}

	for (std::size_t first = 0; first < outputs.size(); ++first)
		for (std::size_t second = first + 1; second < outputs.size(); ++second)
		{
			auto difference = 0.0f;
			for (std::size_t sample = 0; sample < outputs[first].size(); ++sample)
				difference = std::max(difference,
					std::abs(outputs[first][sample] - outputs[second][sample]));
			REQUIRE(difference > 1.0e-3f);
		}
}

TEST_CASE("Rav mode stage unimplemented candidate selections preserve Legacy rendering", "[dsp][rav][baseline]")
{
	auto render = [](vekt::rav::RavProcessingModel model)
	{
		vekt::rav::RavModeStage stage;
		stage.setProcessingModel(model);
		stage.prepare(192'000.0);
		stage.setParameters(vekt::rav::RavMode::gatedFuzz, 30.0f, -0.2f, 0.7f, 0.4f, 0.8f, 3.0f);
		std::array<float, 256> samples {};
		for (std::size_t sample = 0; sample < samples.size(); ++sample)
			samples[sample] = 0.25f * std::sin(static_cast<float>(sample) * 0.13f);
		stage.process(samples);
		return samples;
	};

	const auto legacy = render(vekt::rav::RavProcessingModel::legacy);
	for (const auto candidate : { vekt::rav::RavProcessingModel::behavioralCandidate,
		vekt::rav::RavProcessingModel::overdriveCircuitCandidate })
	{
		const auto selected = render(candidate);
		for (std::size_t sample = 0; sample < legacy.size(); ++sample)
			REQUIRE(selected[sample] == Catch::Approx(legacy[sample]).margin(1.0e-7f));
	}
}

TEST_CASE("Rav Circuit Fuzz is finite and deterministic", "[dsp][rav][circuit-fuzz]")
{
	auto render = [](double sampleRate)
	{
		vekt::rav::RavModeStage stage;
		stage.prepare(sampleRate);
		stage.setParameters(vekt::rav::RavMode::circuitFuzz, 36.0f, -0.65f, 1.0f, 0.0f, 1.0f, 6.0f);
		std::vector<float> samples(static_cast<std::size_t>(sampleRate * 0.03));
		for (std::size_t sample = 0; sample < samples.size(); ++sample)
			samples[sample] = 0.9f * std::sin(static_cast<float>(sample) * 0.21f);
		stage.process(samples);
		return samples;
	};

	const auto first = render(48'000.0);
	const auto second = render(48'000.0);
	for (std::size_t sample = 0; sample < first.size(); ++sample)
	{
		REQUIRE(std::isfinite(first[sample]));
		REQUIRE(second[sample] == Catch::Approx(first[sample]).margin(1.0e-7f));
	}
}

TEST_CASE("Rav Circuit Fuzz has rate-consistent recovery", "[dsp][rav][circuit-fuzz][rate]")
{
	auto renderRecovery = [](double sampleRate)
	{
		vekt::rav::RavModeStage stage;
		stage.prepare(sampleRate);
		stage.setParameters(vekt::rav::RavMode::circuitFuzz, 24.0f, 0.2f, 0.8f, 0.35f, 0.9f, 0.0f);
		std::vector<float> excitation(static_cast<std::size_t>(sampleRate * 0.01), 0.35f);
		stage.process(excitation);
		std::vector<float> recovery(static_cast<std::size_t>(sampleRate * 0.02), 0.0f);
		stage.process(recovery);
		return recovery.back();
	};

	const auto reference = renderRecovery(192'000.0);
	const auto lowerRate = renderRecovery(48'000.0);
	REQUIRE(lowerRate == Catch::Approx(reference).margin(0.02f));
}

TEST_CASE("Rav Circuit Fuzz differs from Gated Fuzz", "[dsp][rav][circuit-fuzz]")
{
	auto render = [](vekt::rav::RavMode mode)
	{
		vekt::rav::RavModeStage stage;
		stage.prepare(192'000.0);
		stage.setParameters(mode, 30.0f, -0.2f, 0.7f, 0.4f, 0.8f, 3.0f);
		std::array<float, 512> samples {};
		for (std::size_t sample = 0; sample < samples.size(); ++sample)
			samples[sample] = 0.25f * std::sin(static_cast<float>(sample) * 0.13f);
		stage.process(samples);
		return samples;
	};

	const auto gated = render(vekt::rav::RavMode::gatedFuzz);
	const auto circuit = render(vekt::rav::RavMode::circuitFuzz);
	auto maximumDifference = 0.0f;
	for (std::size_t sample = 0; sample < gated.size(); ++sample)
		maximumDifference = std::max(maximumDifference, std::abs(gated[sample] - circuit[sample]));
	REQUIRE(maximumDifference > 1.0e-3f);
}

TEST_CASE("Rav mode stage supports slow Bias modulation", "[dsp][rav][bias]")
{
	vekt::rav::RavModeStage stage;
	stage.prepare(48'000.0);
	stage.setArtifactSafePolicy(true);
	stage.setParameters(vekt::rav::RavMode::gatedFuzz, 24.0f, 1.0f, 0.7f, 0.4f, 0.8f);
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
	stage.setParameters(vekt::rav::RavMode::gatedFuzz, 24.0f, 0.0f, 0.7f, 0.4f, 1.0f);
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
		stage.setParameters(vekt::rav::RavMode::gatedFuzz, 24.0f, 0.2f, 0.7f, 0.4f, 0.8f, 6.0f);
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
	stage.setParameters(vekt::rav::RavMode::gatedFuzz, 6.0f, 0.0f, 0.5f, 0.5f, 0.5f);
	std::array first { 0.2f, -0.2f, 0.2f, -0.2f };
	stage.process(first);
	stage.setParameters(vekt::rav::RavMode::gatedFuzz, 36.0f, 0.0f, 0.5f, 0.5f, 0.5f);
	std::array second { 0.2f, -0.2f, 0.2f, -0.2f };
	stage.process(second);

	for (const auto sample : second)
		REQUIRE(std::isfinite(sample));
}
