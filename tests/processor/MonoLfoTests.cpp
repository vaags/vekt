#include "Lfo.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace
{
using vekt::mono::Lfo;
using vekt::mono::LfoClock;
using vekt::mono::LfoMode;
using vekt::mono::LfoPolarity;
using vekt::mono::LfoShape;

constexpr double sampleRate = 48'000.0;

Lfo makeLfo(Lfo::Parameters parameters, std::uint32_t voiceSeed = 1, double rate = sampleRate)
{
	Lfo lfo;
	lfo.setSampleRate(rate);
	lfo.setParameters(parameters);
	lfo.reset(voiceSeed, 99);
	lfo.noteOn();
	return lfo;
}

std::vector<float> render(Lfo& lfo, int samples, LfoClock* clock = nullptr)
{
	std::vector<float> values;
	values.reserve(static_cast<std::size_t>(samples));
	for (int sample = 0; sample < samples; ++sample)
	{
		values.push_back(lfo.getNextSample(clock != nullptr ? clock->getPosition() : 0.0));
		if (clock != nullptr) clock->advance();
	}
	return values;
}
}

TEST_CASE("Mono LFO shapes reach their named values", "[mono][lfo]")
{
	const auto at = [](LfoShape shape, double phase) { return Lfo::bipolarShape(shape, phase, 0); };
	REQUIRE(at(LfoShape::sine, 0.0) == Catch::Approx(0.0).margin(1.0e-6));
	REQUIRE(at(LfoShape::sine, 0.25) == Catch::Approx(1.0));
	REQUIRE(at(LfoShape::sine, 0.75) == Catch::Approx(-1.0));
	REQUIRE(at(LfoShape::triangle, 0.0) == Catch::Approx(0.0));
	REQUIRE(at(LfoShape::triangle, 0.25) == Catch::Approx(1.0));
	REQUIRE(at(LfoShape::triangle, 0.75) == Catch::Approx(-1.0));
	REQUIRE(at(LfoShape::sawUp, 0.0) == Catch::Approx(-1.0));
	REQUIRE(at(LfoShape::sawUp, 0.5) == Catch::Approx(0.0));
	REQUIRE(at(LfoShape::sawDown, 0.0) == Catch::Approx(1.0));
	REQUIRE(at(LfoShape::sawDown, 0.5) == Catch::Approx(0.0));
	REQUIRE(at(LfoShape::square, 0.25) == 1.0f);
	REQUIRE(at(LfoShape::square, 0.75) == -1.0f);
	// Shapes repeat every cycle.
	for (const auto shape : { LfoShape::sine, LfoShape::triangle, LfoShape::sawUp, LfoShape::sawDown, LfoShape::square })
		REQUIRE(at(shape, 3.3) == Catch::Approx(at(shape, 0.3)).margin(1.0e-5));
}

TEST_CASE("Mono LFO polarity maps bipolar and unipolar ranges", "[mono][lfo]")
{
	for (const auto shape : { LfoShape::sine, LfoShape::triangle, LfoShape::sawUp, LfoShape::sawDown, LfoShape::square, LfoShape::smoothRandom })
	{
		CAPTURE(static_cast<int>(shape));
		auto bipolar = makeLfo({ .rateHz = 7.0f, .shape = shape, .mode = LfoMode::retrigger });
		auto unipolar = makeLfo({ .rateHz = 7.0f, .shape = shape, .polarity = LfoPolarity::unipolar, .mode = LfoMode::retrigger });
		const auto bipolarValues = render(bipolar, 48'000);
		const auto unipolarValues = render(unipolar, 48'000);
		for (std::size_t index = 0; index < bipolarValues.size(); ++index)
		{
			REQUIRE(bipolarValues[index] >= -1.0f);
			REQUIRE(bipolarValues[index] <= 1.0f);
			REQUIRE(unipolarValues[index] == Catch::Approx(0.5f * (bipolarValues[index] + 1.0f)).margin(1.0e-6));
		}
	}
}

TEST_CASE("Mono LFO rate sets the cycle length and is clamped to its range", "[mono][lfo]")
{
	// A 4 Hz saw up wraps from +1 to -1 once every 12,000 samples at 48 kHz.
	auto lfo = makeLfo({ .rateHz = 4.0f, .shape = LfoShape::sawUp, .mode = LfoMode::retrigger });
	const auto values = render(lfo, 48'000);
	std::vector<int> wraps;
	for (std::size_t index = 1; index < values.size(); ++index)
		if (values[index] < values[index - 1] - 1.0f) wraps.push_back(static_cast<int>(index));
	REQUIRE(wraps.size() == 3);
	for (const auto wrap : wraps) REQUIRE(wrap % 12'000 <= 1);

	auto slow = makeLfo({ .rateHz = 0.0001f, .shape = LfoShape::sawUp, .mode = LfoMode::retrigger });
	auto slowValues = render(slow, 48'000);
	REQUIRE(slowValues.back() - slowValues.front() == Catch::Approx(2.0 * vekt::mono::minimumLfoRateHz).margin(1.0e-4));
	auto fast = makeLfo({ .rateHz = 500.0f, .shape = LfoShape::sawUp, .mode = LfoMode::retrigger });
	const auto fastValues = render(fast, 48'000);
	int fastWraps {};
	for (std::size_t index = 1; index < fastValues.size(); ++index)
		if (fastValues[index] < fastValues[index - 1] - 1.0f) ++fastWraps;
	REQUIRE(fastWraps == Catch::Approx(vekt::mono::maximumLfoRateHz).margin(1.0));
}

TEST_CASE("Mono LFO retrigger restarts each note at the start phase", "[mono][lfo]")
{
	auto lfo = makeLfo({ .rateHz = 3.0f, .shape = LfoShape::sine, .mode = LfoMode::retrigger, .phase = 0.25f });
	REQUIRE(lfo.getNextSample(0.0) == Catch::Approx(1.0));
	render(lfo, 7'001);
	lfo.noteOn();
	REQUIRE(lfo.getNextSample(0.0) == Catch::Approx(1.0));
}

TEST_CASE("Mono LFO free mode follows the shared clock so voices stay in step", "[mono][lfo]")
{
	LfoClock clock;
	clock.setSampleRate(sampleRate);
	clock.setRate(2.5f);
	const Lfo::Parameters parameters { .rateHz = 2.5f, .shape = LfoShape::smoothRandom, .mode = LfoMode::free };
	auto first = makeLfo(parameters, 1);
	auto second = makeLfo(parameters, 2);
	for (int sample = 0; sample < 30'000; ++sample)
	{
		if (sample == 12'345) second.noteOn();
		const auto position = clock.getPosition();
		REQUIRE(first.getNextSample(position) == second.getNextSample(position));
		clock.advance();
	}

	// Phase offsets the free-running LFO relative to the clock.
	auto shifted = makeLfo({ .rateHz = 2.5f, .shape = LfoShape::sawUp, .mode = LfoMode::free, .phase = 0.5f });
	REQUIRE(shifted.getNextSample(0.0) == Catch::Approx(0.0).margin(1.0e-6));
	REQUIRE(shifted.getNextSample(0.25) == Catch::Approx(0.5));
}

TEST_CASE("Mono LFO one shot runs one cycle and holds its final value", "[mono][lfo]")
{
	// Unipolar saw down in One Shot is a linear decay envelope from 1 to 0.
	auto lfo = makeLfo({ .rateHz = 4.0f, .shape = LfoShape::sawDown, .polarity = LfoPolarity::unipolar, .mode = LfoMode::oneShot });
	const auto values = render(lfo, 24'000);
	REQUIRE(values.front() == Catch::Approx(1.0));
	for (std::size_t index = 1; index < 12'000; ++index) REQUIRE(values[index] < values[index - 1]);
	REQUIRE(lfo.isFinished());
	REQUIRE(values[11'999] == Catch::Approx(0.0).margin(1.0e-3));
	for (std::size_t index = 12'000; index < values.size(); ++index) REQUIRE(values[index] == values[11'999]);

	lfo.noteOn();
	REQUIRE_FALSE(lfo.isFinished());
	REQUIRE(lfo.getNextSample(0.0) == Catch::Approx(1.0));
}

TEST_CASE("Mono LFO delay holds silence and fade ramps linearly", "[mono][lfo]")
{
	// A unipolar square is a constant 1 for the first half cycle, exposing the gain envelope.
	auto lfo = makeLfo({ .rateHz = 0.01f, .shape = LfoShape::square, .polarity = LfoPolarity::unipolar,
		.mode = LfoMode::retrigger, .delaySeconds = 0.1f, .fadeSeconds = 0.2f });
	const auto values = render(lfo, 20'000);
	for (std::size_t index = 0; index < 4'800; ++index) REQUIRE(values[index] == 0.0f);
	REQUIRE(values[4'800 + 4'800] == Catch::Approx(0.5).margin(1.0e-3));
	REQUIRE(values[4'800 + 9'599] == Catch::Approx(1.0));
	REQUIRE(values.back() == 1.0f);

	// Free mode also delays and fades per note, while its phase keeps following the clock.
	LfoClock clock;
	clock.setSampleRate(sampleRate);
	clock.setRate(1.0f);
	clock.setPosition(0.3);
	auto free = makeLfo({ .rateHz = 1.0f, .shape = LfoShape::sawUp, .mode = LfoMode::free, .delaySeconds = 0.01f });
	const auto freeValues = render(free, 481, &clock);
	REQUIRE(freeValues[479] == 0.0f);
	REQUIRE(freeValues[480] == Catch::Approx(Lfo::bipolarShape(LfoShape::sawUp, 0.3 + 480.0 / sampleRate, 0)));
}

TEST_CASE("Mono LFO smooth random is continuous, bounded and seeded", "[mono][lfo]")
{
	const Lfo::Parameters parameters { .rateHz = 10.0f, .shape = LfoShape::smoothRandom, .mode = LfoMode::retrigger };
	auto first = makeLfo(parameters, 1);
	auto repeat = makeLfo(parameters, 1);
	auto other = makeLfo(parameters, 2);
	const auto values = render(first, 96'000);
	REQUIRE(values == render(repeat, 96'000));
	REQUIRE(values != render(other, 96'000));
	float largestStep {}, lowest { 1.0f }, highest { -1.0f };
	for (std::size_t index = 1; index < values.size(); ++index)
	{
		largestStep = std::max(largestStep, std::abs(values[index] - values[index - 1]));
		lowest = std::min(lowest, values[index]);
		highest = std::max(highest, values[index]);
	}
	// A cosine segment moves at most pi/2 * 2 / 4800 per sample at 10 Hz.
	REQUIRE(largestStep < 7.0e-4f);
	REQUIRE(lowest >= -1.0f);
	REQUIRE(highest <= 1.0f);
	REQUIRE(highest - lowest > 0.5f);
}

TEST_CASE("Mono LFO drift is off at zero and subtle at maximum", "[mono][lfo]")
{
	const auto voices = [](Lfo::Parameters parameters, int samples)
	{
		auto first = makeLfo(parameters, 1);
		auto second = makeLfo(parameters, 2);
		return std::pair { render(first, samples), render(second, samples) };
	};
	const auto [cleanFirst, cleanSecond] = voices({ .rateHz = 5.0f, .shape = LfoShape::smoothRandom, .mode = LfoMode::free }, 4'800);
	REQUIRE(cleanFirst == cleanSecond);
	const auto [driftFirst, driftSecond] = voices({ .rateHz = 5.0f, .shape = LfoShape::sine, .mode = LfoMode::retrigger, .drift = 1.0f }, 4'800);
	REQUIRE(driftFirst != driftSecond);

	for (std::uint32_t seed = 1; seed <= 8; ++seed)
	{
		CAPTURE(seed);
		// Level stays within 3% of full scale.
		auto sine = makeLfo({ .rateHz = 5.0f, .shape = LfoShape::sine, .mode = LfoMode::retrigger, .drift = 1.0f }, seed);
		float peak {};
		for (const auto value : render(sine, 48'000)) peak = std::max(peak, std::abs(value));
		REQUIRE(peak == Catch::Approx(1.0).margin(0.031));
		// Rate stays within 2%: a 5 Hz saw wraps 50 +/- 1 times in ten seconds.
		auto saw = makeLfo({ .rateHz = 5.0f, .shape = LfoShape::sawUp, .mode = LfoMode::retrigger, .drift = 1.0f }, seed);
		const auto values = render(saw, 480'000);
		int wraps {};
		for (std::size_t index = 1; index < values.size(); ++index)
			if (values[index] < values[index - 1] - 1.0f) ++wraps;
		REQUIRE(wraps >= 49);
		REQUIRE(wraps <= 51);
	}
}

TEST_CASE("Mono LFO timing is independent of sample rate", "[mono][lfo]")
{
	const Lfo::Parameters parameters { .rateHz = 3.0f, .shape = LfoShape::triangle, .mode = LfoMode::retrigger,
		.delaySeconds = 0.05f, .fadeSeconds = 0.1f };
	auto base = makeLfo(parameters, 1, 48'000.0);
	auto doubled = makeLfo(parameters, 1, 96'000.0);
	const auto baseValues = render(base, 48'000);
	const auto doubledValues = render(doubled, 96'000);
	for (std::size_t index = 0; index < baseValues.size(); index += 997)
		REQUIRE(doubledValues[index * 2] == Catch::Approx(baseValues[index]).margin(2.0e-3));
}
