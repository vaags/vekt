#include <vekt/dsp/DisplayHistory.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <functional>
#include <numbers>
#include <random>
#include <vector>

namespace
{
using History = vekt::dsp::DisplayHistory<1>;
using Timeline = vekt::dsp::DisplayTimeline<1>;
constexpr auto sampleRate = 48'000.0;

// A 3 Hz sine of the sample position: what the audio side publishes at each block's end.
float signalAt(double sample) { return static_cast<float>(std::sin(2.0 * std::numbers::pi * 3.0 * sample / sampleRate)); }

History::Frame frameAt(std::uint64_t sample, std::uint64_t tag = 1)
{
	History::Frame frame;
	frame.sample = sample;
	frame.sampleRate = sampleRate;
	frame.tag = tag;
	frame.values[0] = signalAt(static_cast<double>(sample));
	return frame;
}

struct DisplayRun
{
	std::vector<double> targets; // the sample position each display frame showed
	std::vector<float> values;
	std::vector<double> frontiers; // the newest published sample at each display frame
	std::vector<double> delays;
};

// Simulates a host and a display: blockSizes() gives each callback's block size, and callbacks arrive in groups of
// `burst` blocks once that much audio is due (burst 1: a regular callback per block). The display presents frames at
// `displayHz` for `seconds`.
DisplayRun run(const std::function<int()>& blockSizes, int burst, double displayHz, double seconds)
{
	History history;
	Timeline timeline;
	DisplayRun result;
	std::uint64_t published {};
	std::vector<int> pending;
	double due {}; // wall time by which the published audio must exist
	for (auto frame = 0; frame < static_cast<int>(seconds * displayHz); ++frame)
	{
		const auto now = static_cast<double>(frame) / displayHz;
		// The host renders whole groups of blocks as soon as the audio they cover is due.
		while (due <= now)
		{
			pending.clear();
			for (auto block = 0; block < burst; ++block) pending.push_back(blockSizes());
			for (const auto size : pending)
			{
				published += static_cast<std::uint64_t>(size);
				history.publish(frameAt(published));
				due += static_cast<double>(size) / sampleRate;
			}
		}
		const auto shown = timeline.advance(history, now);
		REQUIRE(shown.has_value());
		result.targets.push_back(static_cast<double>(shown->sample));
		result.values.push_back(shown->values[0]);
		result.frontiers.push_back(static_cast<double>(published));
		result.delays.push_back(timeline.getDelaySeconds());
	}
	return result;
}

// After the first second (while the delay settles): the shown time never goes back or past what was published,
// advances at a steady pace each frame (no stutter: no frame repeats or jumps), and shows the signal at that time to
// within linear interpolation's error over the longest block.
void requireSmooth(const DisplayRun& result, double displayHz, double maximumDelaySeconds, int longestBlock)
{
	const auto angularStep = 2.0 * std::numbers::pi * 3.0 * longestBlock / sampleRate;
	const auto interpolationError = angularStep * angularStep / 8.0 + 1.0e-3;
	const auto settled = static_cast<std::size_t>(displayHz);
	const auto step = sampleRate / displayHz;
	for (std::size_t frame = settled; frame < result.targets.size(); ++frame)
	{
		INFO("Frame " << frame);
		REQUIRE(result.targets[frame] >= result.targets[frame - 1]);
		REQUIRE(result.targets[frame] <= result.frontiers[frame]);
		REQUIRE(result.targets[frame] - result.targets[frame - 1] == Catch::Approx(step).epsilon(0.25));
		REQUIRE(result.values[frame] == Catch::Approx(signalAt(result.targets[frame])).margin(interpolationError));
		REQUIRE(result.delays[frame] <= maximumDelaySeconds);
	}
}
}

TEST_CASE("Display history hands the reader each block once, in order, and drops what it cannot keep", "[dsp][display]")
{
	History history;
	std::vector<std::uint64_t> seen;
	const auto collect = [&] { history.readNew([&](const History::Frame& frame) { seen.push_back(frame.sample); }); };
	for (std::uint64_t block = 1; block <= 3; ++block) history.publish(frameAt(block * 512));
	collect();
	REQUIRE(seen == std::vector<std::uint64_t> { 512, 1'024, 1'536 });
	seen.clear();
	collect();
	REQUIRE(seen.empty());
	// A reader that falls more than a full history behind keeps only the newest 64.
	for (std::uint64_t block = 4; block < 104; ++block) history.publish(frameAt(block * 512));
	collect();
	REQUIRE(seen.size() == 64);
	REQUIRE(seen.front() == 40 * 512);
	REQUIRE(seen.back() == 103 * 512);
}

TEST_CASE("Display timeline moves smoothly at any refresh rate with regular blocks", "[dsp][display]")
{
	for (const auto displayHz : { 60.0, 120.0, 144.0 })
		for (const auto blockSize : { 64, 512, 2'048 })
		{
			INFO(displayHz << " Hz display, " << blockSize << "-sample blocks");
			const auto result = run([blockSize] { return blockSize; }, 1, displayHz, 4.0);
			// About a block plus the margin.
			requireSmooth(result, displayHz, static_cast<double>(blockSize) / sampleRate + 0.012, blockSize);
		}
}

TEST_CASE("Display timeline stays smooth with uneven and bursty hosts", "[dsp][display]")
{
	// Block sizes that change every callback (hosts that split blocks at automation or MIDI).
	std::mt19937 random(11);
	std::uniform_int_distribution<int> sizes(32, 1'024);
	auto result = run([&] { return sizes(random); }, 1, 120.0, 4.0);
	requireSmooth(result, 120.0, 1'024.0 / sampleRate + 0.012, 1'024);
	// Four 512-sample blocks rendered back to back every four block periods (large process buffers).
	result = run([] { return 512; }, 4, 120.0, 4.0);
	requireSmooth(result, 120.0, 4.0 * 512.0 / sampleRate + 0.012, 512);
}

TEST_CASE("Display timeline delay follows the host's block size down again", "[dsp][display]")
{
	History history;
	Timeline timeline;
	std::uint64_t published {};
	double due {};
	auto blockSize = 2'048;
	for (auto frame = 0; frame < 120 * 12; ++frame)
	{
		const auto now = frame / 120.0;
		// The user lowers the buffer after four seconds.
		if (frame == 120 * 4) blockSize = 128;
		while (due <= now)
		{
			published += static_cast<std::uint64_t>(blockSize);
			history.publish(frameAt(published));
			due += blockSize / sampleRate;
		}
		static_cast<void>(timeline.advance(history, now));
		if (frame == 120 * 4 - 1) REQUIRE(timeline.getDelaySeconds() > 1'024.0 / sampleRate);
	}
	REQUIRE(timeline.getDelaySeconds() < 0.012);
}

TEST_CASE("Display timeline does not blend across tags and restarts with the audio", "[dsp][display]")
{
	History history;
	Timeline timeline;
	// Two blocks of one voice, then a new voice: between them the old voice's value holds, unblended.
	auto first = frameAt(512, 1);
	first.values[0] = 0.0f;
	auto second = frameAt(1'024, 1);
	second.values[0] = 0.2f;
	auto third = frameAt(1'536, 2);
	third.values[0] = 0.9f;
	for (const auto& frame : { first, second, third }) history.publish(frame);
	auto shown = timeline.advance(history, 10.0);
	// Anchored at the newest block and shown a little before it: inside the voice change, so the earlier voice holds.
	REQUIRE(shown->sample < 1'536);
	REQUIRE(shown->values[0] == Catch::Approx(0.2f));
	REQUIRE(shown->tag == 1);

	// The audio side restarts its count (prepareToPlay): the timeline starts again from the new blocks.
	auto restarted = frameAt(512, 3);
	restarted.values[0] = -0.5f;
	history.publish(restarted);
	shown = timeline.advance(history, 10.01);
	REQUIRE(shown->tag == 3);
	REQUIRE(shown->values[0] == Catch::Approx(-0.5f));

	// After a long gap in display frames (the editor was hidden), it re-anchors instead of lagging far behind.
	std::uint64_t published = 512;
	for (auto block = 0; block < 400; ++block)
	{
		published += 512;
		history.publish(frameAt(published, 3));
	}
	static_cast<void>(timeline.advance(history, 30.0));
	REQUIRE(timeline.getDelaySeconds() < 0.012);
}
