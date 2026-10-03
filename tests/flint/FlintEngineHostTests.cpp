#include "AllocationCounter.h"
#include "FlintEngineHost.h"
#include "FlintEngines.h"

#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace
{
using vekt::flint::FlintEngine;
using vekt::flint::FlintEngineHost;
using vekt::flint::FlintEngines;
using vekt::flint::FlintParameters;
using vekt::flint::Mode;
using vekt::flint::ModelId;
using vekt::flint::Strike;

constexpr double sampleRate = 44'100.0;
constexpr int blockSize = 64;
// The switch fade: 5 ms.
const auto fadeSamples = std::round(0.005 * sampleRate);

// A decaying sine that counts how the host drives it.
class StubEngine final : public FlintEngine
{
public:
	explicit StubEngine(double frequencyHz) : frequency(frequencyHz) {}

	void prepare(double newSampleRate, int) noexcept override { rate = newSampleRate; }
	void reset() noexcept override
	{
		amplitude = 0.0;
		phase = 0.0;
		++resets;
	}
	void activate(const FlintParameters&) noexcept override { ++activations; }
	void update(const FlintParameters&) noexcept override {}
	void trigger(Strike strike) noexcept override { amplitude += strike.velocity; }
	void release(int) noexcept override {}
	void process(std::span<float> left, std::span<float> right) noexcept override
	{
		++processCalls;
		// T60 0.2 s.
		const auto decay = std::pow(10.0, -3.0 / (0.2 * rate));
		for (std::size_t index = 0; index < left.size(); ++index)
		{
			const auto value = static_cast<float>(amplitude * std::sin(phase));
			left[index] = value;
			right[index] = value;
			phase += 2.0 * std::numbers::pi * frequency / rate;
			amplitude *= decay;
		}
		if (amplitude < 1.0e-7) amplitude = 0.0;
	}
	[[nodiscard]] bool isActive() const noexcept override { return amplitude > 0.0; }
	[[nodiscard]] double energy() const noexcept override { return amplitude * amplitude; }

	int processCalls {};
	int activations {};
	int resets {};

private:
	double frequency;
	double rate { sampleRate };
	double amplitude {};
	double phase {};
};

struct Fixture
{
	Fixture()
	{
		FlintEngines engines;
		auto kickEngine = std::make_unique<StubEngine>(55.0);
		auto malletEngine = std::make_unique<StubEngine>(523.0);
		kick = kickEngine.get();
		mallet = malletEngine.get();
		engines[vekt::flint::indexOf(ModelId::kickClassicAnalog)] = std::move(kickEngine);
		engines[vekt::flint::indexOf(ModelId::malletBar)] = std::move(malletEngine);
		host = std::make_unique<FlintEngineHost>(std::move(engines));
		host->prepare(sampleRate, blockSize, sampleRate);
		select(Mode::kick);
	}

	void select(Mode mode) { parameters.mode = mode; }

	// Renders `blocks` blocks, updating once per block; appends the left channel to `output`.
	void render(int blocks, bool selectionMayChange = true)
	{
		for (auto block = 0; block < blocks; ++block)
		{
			host->update(parameters, selectionMayChange);
			if (!host->isSleeping()) host->renderInternal(left, right);
			host->finishHost(left, right);
			output.insert(output.end(), left.begin(), left.end());
		}
	}

	FlintParameters parameters;
	std::unique_ptr<FlintEngineHost> host;
	StubEngine* kick {};
	StubEngine* mallet {};
	std::vector<float> output;
	std::vector<float> left = std::vector<float>(blockSize);
	std::vector<float> right = std::vector<float>(blockSize);
};

float largestStep(const std::vector<float>& samples, std::size_t begin, std::size_t end)
{
	auto step = 0.0f;
	for (auto index = std::max<std::size_t>(begin, 1); index < std::min(end, samples.size()); ++index)
		step = std::max(step, std::abs(samples[index] - samples[index - 1]));
	return step;
}
}

TEST_CASE("Flint engine host processes only the selected engine", "[flint][engine-host]")
{
	Fixture fixture;
	fixture.render(1);
	fixture.host->trigger({ 0.5, 36, 1 });
	fixture.render(200);
	REQUIRE(fixture.kick->processCalls > 0);
	REQUIRE(fixture.mallet->processCalls == 0);
	REQUIRE(fixture.host->selectedModel() == ModelId::kickClassicAnalog);
}

TEST_CASE("Flint engine host falls to exact silence and stops processing after a tail", "[flint][engine-host]")
{
	Fixture fixture;
	fixture.host->setDownsamplingTail(48);
	fixture.render(1);
	fixture.host->trigger({ 0.5, 36, 1 });
	// The stub rings for about 0.47 s to 1e-7 and the 5 Hz DC blocker then decays below 1e-9: well within 2 s.
	fixture.render(1'400);
	REQUIRE(fixture.host->isSleeping());
	const auto callsAtSleep = fixture.kick->processCalls;
	const auto sleptAt = fixture.output.size();
	fixture.render(100);
	REQUIRE(fixture.kick->processCalls == callsAtSleep);
	REQUIRE(std::all_of(fixture.output.begin() + static_cast<std::ptrdiff_t>(sleptAt), fixture.output.end(),
	    [](float sample) { return juce::exactlyEqual(sample, 0.0f); }));

	// A new strike wakes it.
	fixture.host->trigger({ 0.5, 36, 2 });
	fixture.render(1);
	REQUIRE_FALSE(fixture.host->isSleeping());
}

TEST_CASE("Flint engine host fades out a ringing model when another is selected", "[flint][engine-host]")
{
	Fixture reference;
	reference.render(1);
	reference.host->trigger({ 0.5, 36, 1 });
	reference.render(40);
	const auto peak = *std::max_element(reference.output.begin(), reference.output.end());

	Fixture fixture;
	fixture.render(1);
	fixture.host->trigger({ 0.5, 36, 1 });
	fixture.render(20);
	const auto switchedAt = fixture.output.size();
	fixture.select(Mode::mallet);
	fixture.render(20);

	// The kick fades over 5 ms instead of stopping: no step beyond the ringing kick's own largest step plus its peak
	// over the fade length (A15).
	const auto ownStep = largestStep(reference.output, switchedAt - 512, switchedAt + 512);
	const auto bound = ownStep + peak / static_cast<float>(fadeSamples);
	REQUIRE(largestStep(fixture.output, switchedAt - 1, switchedAt + 512) <= bound);
	REQUIRE(std::abs(fixture.output[switchedAt + 64]) > 0.0f); // still fading, not cut
	REQUIRE(fixture.host->selectedModel() == ModelId::malletBar);
	// Once faded, the kick is reset and no longer processed.
	REQUIRE(fixture.kick->resets >= 2);
	const auto kickCalls = fixture.kick->processCalls;
	fixture.render(20);
	REQUIRE(fixture.kick->processCalls == kickCalls);
}

TEST_CASE("Flint engine host reverses a fade when the previous model is selected again", "[flint][engine-host]")
{
	Fixture fixture;
	fixture.render(1);
	fixture.host->trigger({ 0.5, 36, 1 });
	fixture.render(20);
	const auto resetsBefore = fixture.kick->resets;
	fixture.select(Mode::mallet);
	fixture.render(1); // 64 samples into the 221-sample fade
	fixture.select(Mode::kick);
	const auto reversedAt = fixture.output.size();
	fixture.render(20);
	REQUIRE(fixture.host->selectedModel() == ModelId::kickClassicAnalog);
	REQUIRE(fixture.kick->resets == resetsBefore); // kept ringing, never restarted
	REQUIRE(std::abs(fixture.output.back()) > 0.0f);
	const auto peak = *std::max_element(fixture.output.begin(), fixture.output.end());
	REQUIRE(largestStep(fixture.output, reversedAt - 64, reversedAt + 512) <=
	    largestStep(fixture.output, 64, reversedAt - 64) + peak / static_cast<float>(fadeSamples));
}

TEST_CASE("Flint engine host lets a third model wait for a running fade", "[flint][engine-host]")
{
	Fixture fixture;
	fixture.render(1);
	fixture.host->trigger({ 0.5, 36, 1 });
	fixture.render(20);
	fixture.select(Mode::mallet);
	fixture.render(1);
	fixture.select(Mode::snare); // Classic Analog snare: no engine yet
	fixture.render(1);
	REQUIRE(fixture.host->selectedModel() == ModelId::malletBar);
	fixture.render(10); // past the fade
	REQUIRE(fixture.host->selectedModel() == ModelId::snareClassicAnalog);
}

TEST_CASE("Flint engine host holds the selection while a preset is being applied", "[flint][engine-host]")
{
	Fixture fixture;
	fixture.render(1);
	fixture.select(Mode::mallet);
	fixture.render(4, false);
	REQUIRE(fixture.host->selectedModel() == ModelId::kickClassicAnalog);
	fixture.render(1, true);
	REQUIRE(fixture.host->selectedModel() == ModelId::malletBar);
}

TEST_CASE("Flint engine host lets a note sound at full level right after an audio reset", "[flint][engine-host]")
{
	Fixture fixture;
	fixture.render(1);
	fixture.host->trigger({ 0.5, 36, 1 });
	fixture.render(20);
	fixture.host->resetAudio();
	fixture.select(Mode::mallet);
	fixture.host->update(fixture.parameters, true);
	fixture.host->trigger({ 0.5, 72, 2 });
	std::vector<float> left(blockSize), right(blockSize);
	fixture.host->renderInternal(left, right);
	// Only the mallet sounds, unfaded: its second sample is the stub's own value.
	const auto expected = static_cast<float>(
	    0.5 * std::pow(10.0, -3.0 / (0.2 * sampleRate)) * std::sin(2.0 * std::numbers::pi * 523.0 / sampleRate));
	REQUIRE(std::abs(left[1] - expected) < 1.0e-6f);
	REQUIRE(fixture.kick->processCalls > 0);
	const auto kickCalls = fixture.kick->processCalls;
	fixture.host->renderInternal(left, right);
	REQUIRE(fixture.kick->processCalls == kickCalls);
}

TEST_CASE(
    "Flint engine host allocates nothing while rendering, switching and resetting", "[flint][engine-host][allocation]")
{
	Fixture fixture;
	fixture.output.reserve(200 * blockSize);
	fixture.render(1);
	vekt::test::AllocationScope scope;
	fixture.host->trigger({ 0.5, 36, 1 });
	fixture.render(20);
	fixture.select(Mode::mallet);
	fixture.render(2);
	fixture.host->trigger({ 0.8, 72, 2 });
	fixture.select(Mode::kick);
	fixture.render(10);
	fixture.parameters.drive = 0.5;
	fixture.render(10);
	fixture.host->resetAudio();
	fixture.render(10);
	const auto counts = scope.stop();
	INFO("allocations " << counts.allocations << ", releases " << counts.releases);
	REQUIRE(counts.none());
}

TEST_CASE("Flint engine host sleeps after a tail with Drive on, for every drive type", "[flint][engine-host]")
{
	for (const auto type : { vekt::flint::DriveType::soft, vekt::flint::DriveType::hard, vekt::flint::DriveType::fold })
	{
		Fixture fixture;
		fixture.parameters.drive = 0.5;
		fixture.parameters.driveType = type;
		fixture.render(1);
		fixture.host->trigger({ 0.5, 36, 1 });
		fixture.render(1'400);
		INFO("drive type " << static_cast<int>(type));
		REQUIRE(fixture.host->isSleeping());
		const auto sleptAt = fixture.output.size();
		fixture.render(50);
		REQUIRE(std::all_of(fixture.output.begin() + static_cast<std::ptrdiff_t>(sleptAt), fixture.output.end(),
		    [](float sample) { return juce::exactlyEqual(sample, 0.0f); }));
	}
}

TEST_CASE("Flint engine host strikes with the current settings after changes made while idle", "[flint][engine-host]")
{
	// A note at the old settings dies away, the settings change while the instance sleeps, and a new note sounds: it is
	// bit-identical to a fresh instance's first note at the new settings, so nothing glides in from the old ones.
	auto render = [](const FlintParameters& before, const FlintParameters& after, bool playBefore)
	{
		FlintEngineHost host(vekt::flint::makeFlintEngines());
		host.prepare(sampleRate, blockSize, sampleRate);
		std::vector<float> left(blockSize), right(blockSize), output;
		auto play = [&](const FlintParameters& settings, int blocks, bool keep)
		{
			for (auto block = 0; block < blocks; ++block)
			{
				host.update(settings, true);
				if (!host.isSleeping()) host.renderInternal(left, right);
				host.finishHost(left, right);
				if (keep) output.insert(output.end(), left.begin(), left.end());
			}
		};
		if (playBefore)
		{
			host.update(before, true);
			host.trigger({ 0.8, 60, 3 });
			play(before, 4'000, false); // about 5.8 s: the default Bar's tail and the DC blocker
			REQUIRE(host.isSleeping());
		}
		host.update(after, true);
		host.trigger({ 0.8, 60, 3 });
		play(after, 40, true);
		return output;
	};
	auto same = [](const std::vector<float>& a, const std::vector<float>& b)
	{ return std::equal(a.begin(), a.end(), b.begin(), [](float x, float y) { return juce::exactlyEqual(x, y); }); };
	// Drive: unchanged and off; changed in amount and curve while driven; switched on while asleep.
	struct DriveChange
	{
		double before, after;
		vekt::flint::DriveType afterType;
	};
	for (const auto mode : { Mode::kick, Mode::mallet })
		for (const auto drive : { DriveChange { 0.0, 0.0, vekt::flint::DriveType::soft },
		         DriveChange { 0.2, 0.6, vekt::flint::DriveType::fold },
		         DriveChange { 0.0, 0.5, vekt::flint::DriveType::hard } })
		{
			FlintParameters before, after;
			before.mode = after.mode = mode;
			before.pitch = 45.0;
			after.pitch = 52.0;
			before.levelDecibels = -30.0;
			after.levelDecibels = 0.0;
			before.tone = 0.2;
			after.tone = 0.8;
			before.drive = drive.before;
			after.drive = drive.after;
			after.driveType = drive.afterType;
			INFO("mode " << static_cast<int>(mode) << ", Drive " << drive.before << " -> " << drive.after);
			REQUIRE(same(render(before, after, true), render(before, after, false)));
		}
}

TEST_CASE("Flint engine host strikes undamped after a damped note died away", "[flint][engine-host]")
{
	// Note Off Damps: a released note's damping must not carry into the next hit on the silent object (A16 a).
	for (const auto mode : { Mode::kick, Mode::mallet })
	{
		FlintParameters settings;
		settings.mode = mode;
		settings.pitch = 60.0;
		settings.variation = 0.0;
		settings.noteOffDamps = true;
		FlintEngineHost host(vekt::flint::makeFlintEngines());
		host.prepare(sampleRate, blockSize, sampleRate);
		std::vector<float> left(blockSize), right(blockSize);
		auto play = [&](int blocks)
		{
			std::vector<float> output;
			for (auto block = 0; block < blocks; ++block)
			{
				host.update(settings, true);
				if (!host.isSleeping()) host.renderInternal(left, right);
				host.finishHost(left, right);
				output.insert(output.end(), left.begin(), left.end());
			}
			return output;
		};
		host.update(settings, true);
		host.trigger({ 0.8, 60, 3 });
		const auto first = play(40);
		host.release(60);
		juce::ignoreUnused(play(4'000));
		REQUIRE(host.isSleeping());
		host.trigger({ 0.8, 60, 3 });
		const auto second = play(40);
		INFO("mode " << static_cast<int>(mode));
		REQUIRE(std::equal(
		    first.begin(), first.end(), second.begin(), [](float x, float y) { return juce::exactlyEqual(x, y); }));
	}
}

TEST_CASE("Flint builds an engine for exactly the models it marks available", "[flint][engine-host]")
{
	const auto engines = vekt::flint::makeFlintEngines();
	for (std::size_t index = 0; index < vekt::flint::modelCount; ++index)
	{
		INFO("model " << index);
		REQUIRE((engines[index] != nullptr) == vekt::flint::isAvailable(static_cast<ModelId>(index)));
	}
}

TEST_CASE("Flint engine host sleeps while Drive is automated through the end of a tail", "[flint][engine-host]")
{
	FlintParameters settings;
	settings.drive = 0.3;
	settings.decay = 0.0;
	FlintEngineHost host(vekt::flint::makeFlintEngines());
	host.prepare(sampleRate, blockSize, sampleRate);
	std::vector<float> left(blockSize), right(blockSize);
	host.update(settings, true);
	host.trigger({ 1.0, 60, 1 });
	for (auto block = 0; block < 2'000 && !host.isSleeping(); ++block)
	{
		settings.drive = block % 2 == 0 ? 0.35 : 0.3; // a fast host automation lane
		host.update(settings, true);
		if (!host.isSleeping()) host.renderInternal(left, right);
		host.finishHost(left, right);
	}
	REQUIRE(host.isSleeping());
}
