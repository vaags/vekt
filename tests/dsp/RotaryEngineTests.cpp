#include <RotaryEngine.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

TEST_CASE("Glimmer pickup geometry separates fixed delay from travel", "[glimmer][engine][latency]")
{
	using vekt::glimmer::RotaryEngine;
	for (const auto rate : { 44'100.0, 48'000.0, 96'000.0, 192'000.0 })
		for (const auto distance : { 0.3f, 1.0f, 3.0f })
		{
			const auto center = static_cast<float>(RotaryEngine::latencySamples(rate));
			REQUIRE(RotaryEngine::pickupDelay(rate, distance, 0, 1) == Catch::Approx(center));
			const auto near = RotaryEngine::pickupDelay(rate, distance, 0.18f, 1);
			const auto far = RotaryEngine::pickupDelay(rate, distance, 0.18f, -1);
			REQUIRE(near > 0);
			REQUIRE(far < RotaryEngine::capacitySamples(rate));
			REQUIRE(far - near == Catch::Approx(0.36 * rate / 343.0).margin(0.001));
		}
}

TEST_CASE("Glimmer width has identity and deliberate mono endpoints", "[glimmer][engine]")
{
	using vekt::glimmer::RotaryEngine;
	const auto unchanged = RotaryEngine::applyWidth({ 0.25f, -0.5f }, 1);
	REQUIRE(unchanged[0] == Catch::Approx(0.25f));
	REQUIRE(unchanged[1] == Catch::Approx(-0.5f));
	const auto mono = RotaryEngine::applyWidth({ 0.25f, -0.5f }, 0);
	REQUIRE(mono[0] == Catch::Approx(-0.125f));
	REQUIRE(mono[1] == Catch::Approx(mono[0]));
}

TEST_CASE("Glimmer Drum is broadband and ignores horn controls", "[glimmer][engine]")
{
	using namespace vekt::glimmer;
	RotaryEngine reference, changed;
	RotarySettings settings;
	settings.brake = true;
	reference.prepare(48000, 128);
	changed.prepare(48000, 128);
	reference.start(CabinetModel::drum, settings);
	settings.hornTone = 12;
	settings.balance = 100;
	changed.start(CabinetModel::drum, settings);
	double energy {};
	for (int sample = 0; sample < 4800; ++sample)
	{
		const auto input = 0.1f * std::sin(static_cast<float>(sample) * 0.3f);
		const auto first = reference.process({ input, -input }, false);
		const auto second = changed.process({ input, -input }, false);
		REQUIRE(first[0] == Catch::Approx(second[0]).margin(1.0e-7f));
		REQUIRE(first[1] == Catch::Approx(second[1]).margin(1.0e-7f));
		energy += first[0] * first[0];
	}
	REQUIRE(energy > 0.01);
}

TEST_CASE("Glimmer models preserve side input and produce distinct responses", "[glimmer][engine]")
{
	using namespace vekt::glimmer;
	std::array<RotaryEngine, 3> engines;
	RotarySettings settings;
	settings.brake = true;
	for (std::size_t model = 0; model < engines.size(); ++model)
	{
		engines[model].prepare(48000, 128);
		engines[model].start(static_cast<CabinetModel>(model), settings);
	}
	std::array<double, 3> energies {}, differences {};
	for (int sample = 0; sample < 10000; ++sample)
	{
		const auto input = 0.1f * std::sin(static_cast<float>(sample) * 0.7f);
		std::array<float, 3> outputs;
		for (std::size_t model = 0; model < engines.size(); ++model)
		{
			const auto output = engines[model].process({ input, -input }, false);
			REQUIRE(std::isfinite(output[0]));
			energies[model] += (output[0] - output[1]) * (output[0] - output[1]);
			outputs[model] = output[0];
		}
		for (std::size_t model = 0; model < engines.size(); ++model)
			differences[model] += std::abs(outputs[model] - outputs[(model + 1) % 3]);
	}
	for (const auto energy : energies) REQUIRE(energy > 0.01);
	for (const auto difference : differences) REQUIRE(difference > 1);
}

TEST_CASE("Glimmer Horn Tone changes spectral balance rather than flat level", "[glimmer][engine][tone]")
{
	using namespace vekt::glimmer;
	std::array<double, 2> ratios {};
	for (std::size_t frequency = 0; frequency < 2; ++frequency)
	{
		RotaryEngine neutral, boosted;
		RotarySettings settings;
		settings.brake = true;
		settings.balance = 100;
		neutral.prepare(48000, 128);
		boosted.prepare(48000, 128);
		neutral.start(CabinetModel::classic, settings);
		settings.hornTone = 12;
		boosted.start(CabinetModel::classic, settings);
		double reference {}, result {};
		for (int sample = 0; sample < 12000; ++sample)
		{
			const auto input = 0.1f * std::sin(static_cast<float>(sample) * (frequency == 0 ? 0.13f : 0.9f));
			const auto first = neutral.process({ input, input }, false)[0];
			const auto second = boosted.process({ input, input }, false)[0];
			if (sample > 2000) { reference += first * first; result += second * second; }
		}
		ratios[frequency] = result / reference;
	}
	REQUIRE(ratios[1] > ratios[0] * 1.5);
}

TEST_CASE("Glimmer angle wrap and repeated preparation remain deterministic", "[glimmer][engine][automation]")
{
	using namespace vekt::glimmer;
	RotaryEngine first, second;
	RotarySettings settings;
	settings.angle = 359;
	first.prepare(48000, 128);
	second.prepare(48000, 128);
	first.start(CabinetModel::classic, settings);
	settings.angle = -1;
	second.start(CabinetModel::classic, settings);
	for (int sample = 0; sample < 8000; ++sample)
	{
		if (sample == 2000)
		{
			settings.angle = 1;
			first.setSettings(settings);
			second.setSettings(settings);
		}
		const auto input = 0.1f * std::sin(static_cast<float>(sample) * 0.1f);
		const auto reference = first.process({ input, input }, false);
		const auto actual = second.process({ input, input }, false);
		REQUIRE(reference[0] == Catch::Approx(actual[0]).margin(2.0e-5f));
		REQUIRE(reference[1] == Catch::Approx(actual[1]).margin(2.0e-5f));
	}
}
