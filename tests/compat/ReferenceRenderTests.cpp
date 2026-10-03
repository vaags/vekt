// Reference renders: each case plays a fixed input (or MIDI phrase) through a fresh processor and
// must match the stored WAV in tests/fixtures/audio to well below audibility. A deliberate sound
// change is accepted by recapturing; anything else that moves these is a regression.
// See tests/fixtures/README.md.

#include "CompatFixtures.h"

#include <vekt/glimmer/Parameters.h>
#include <vekt/kobber/Parameters.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <set>

namespace
{
using namespace vekt::compat;
namespace glimmer = vekt::glimmer::parameters;
namespace kobber = vekt::kobber::parameters;

constexpr auto sampleRate = 48'000.0;
constexpr auto blockSize = 256;
constexpr auto frames = 9'600; // 0.2 s
// About -94 dBFS: far below audibility, far above float reordering noise (~1e-7).
constexpr auto tolerance = 2.0e-5f;

struct RenderCase
{
	std::string product;
	std::string name;
	std::vector<std::pair<std::string, float>> settings; // plain parameter values
	bool offline {};
};

// Rav and Glimmer oversampling choices, by index, excluding the default each product renders anyway.
std::vector<RenderCase> oversamplingCases(const std::string& product, const char* trackingId, int defaultIndex)
{
	static constexpr std::array names { "off", "2x-iir", "4x-iir", "2x-fir", "4x-fir", "8x-fir", "16x-fir" };
	std::vector<RenderCase> cases;
	for (auto index = 0; index < static_cast<int>(names.size()); ++index)
		if (index != defaultIndex)
			cases.push_back({ product, std::string("oversampling-") + names[static_cast<std::size_t>(index)], { { trackingId, static_cast<float>(index) } } });
	return cases;
}

const std::vector<RenderCase>& renderCases()
{
	static const auto cases = []
	{
		std::vector<RenderCase> all {
			{ "rav", "saturation", { { "mode", 0.0f }, { "drive", 18.0f } } },
			{ "rav", "overdrive", { { "mode", 1.0f }, { "drive", 18.0f } } },
			{ "rav", "distortion", { { "mode", 2.0f }, { "drive", 18.0f } } },
			{ "rav", "circuit-fuzz", { { "mode", 3.0f }, { "drive", 18.0f } } },
			{ "rav", "gated-fuzz", { { "mode", 4.0f }, { "drive", 18.0f } } },
			{ "rav", "multiband-tone-mix", { { "lowBandMix", 30.0f }, { "highBandMix", 80.0f }, { "lowMidCutoffHz", 300.0f },
				{ "tone", 0.6f }, { "mix", 70.0f }, { "bias", 0.3f } } },
			// Offline renders must follow the offline choice, not the tracking one (which is off here).
			{ "rav", "offline-4x-iir", { { "trackingOversampling", 0.0f }, { "offlineOversampling", 6.0f } }, true },

			{ "glimmer", "drum", { { glimmer::cabinetModel, 1.0f } } },
			{ "glimmer", "wide", { { glimmer::cabinetModel, 2.0f } } },
			{ "glimmer", "fast", { { glimmer::speedMode, 1.0f } } },
			{ "glimmer", "auto", { { glimmer::speedMode, 2.0f } } },
			{ "glimmer", "brake-driven", { { glimmer::brake, 1.0f }, { glimmer::preampDrive, 18.0f } } },
			// The Classic model at its default tracking quality, reached only through the offline choice.
			{ "glimmer", "offline-4x-iir", { { glimmer::trackingOversampling, 0.0f }, { glimmer::offlineOversampling, 6.0f } }, true },

			{ "kobber", "ladder", {} },
			{ "kobber", "ladder-resonant-2x", { { kobber::trackingOversampling, 1.0f }, { kobber::filterResonance, 70.0f }, { kobber::filterCutoff, 900.0f } } },
			{ "kobber", "ladder-4x", { { kobber::trackingOversampling, 4.0f } } },
			{ "kobber", "ladder-8x", { { kobber::trackingOversampling, 5.0f } } },
			// An offline render at the default Offline choice (4x FIR), not the tracking one (16x here; ADR 0001).
			{ "kobber", "offline-default-resonant", { { kobber::trackingOversampling, 6.0f }, { kobber::filterResonance, 70.0f },
				{ kobber::filterCutoff, 900.0f } }, true },
			{ "kobber", "svf-bandpass", { { kobber::filterType, 1.0f }, { kobber::filterMode, 0.0f }, { kobber::filterResonance, 50.0f } } },
			{ "kobber", "k35", { { kobber::filterType, 2.0f }, { kobber::filterResonance, 60.0f } } },
			{ "kobber", "unison-noise-lfo", { { kobber::unison, 2.0f }, { kobber::noiseType, 2.0f }, { kobber::noiseLevel, 30.0f },
				{ kobber::lfos[0].rate, 6.0f }, { kobber::lfos[0].pitch[0], 2.0f } } },
			{ "kobber", "legato-glide", { { kobber::performanceMode, 2.0f }, { kobber::glideMode, 1.0f }, { kobber::glideTime, 0.05f } } },
			// Mono, low-note priority: the higher second note waits until the first is released (held-key return).
			{ "kobber", "low-priority", { { kobber::performanceMode, 1.0f }, { kobber::notePriority, 1.0f } } },
			// Oscillators 2 and 3 sounding at 16' and 1'.
			{ "kobber", "osc-ranges", { { kobber::osc2Range, 0.0f }, { kobber::osc2Level, 60.0f }, { kobber::osc3Range, 4.0f },
				{ kobber::osc3Level, 40.0f } } },
		};
		for (auto&& extra : oversamplingCases("rav", "trackingOversampling", 2)) all.push_back(extra);
		for (auto&& extra : oversamplingCases("glimmer", glimmer::trackingOversampling, 2)) all.push_back(extra);
		return all;
	}();
	return cases;
}

// A sweep from 80 Hz to 6 kHz with a decaying noise burst halfway, slightly different per channel.
float effectInput(int channel, int frame)
{
	const auto time = frame / sampleRate;
	const auto duration = frames / sampleRate;
	const auto ratio = 6'000.0 / 80.0;
	const auto phase = 2.0 * std::numbers::pi * 80.0 * duration / std::log(ratio) * (std::pow(ratio, time / duration) - 1.0);
	auto value = 0.3 * std::sin(phase + 0.5 * channel);
	if (frame >= frames / 2)
	{
		auto state = static_cast<std::uint32_t>(frame * 2 + channel) * 747'796'405u + 2'891'336'453u;
		state ^= state >> 16;
		state *= 2'246'822'519u;
		state ^= state >> 13;
		const auto noise = static_cast<double>(state) / 4'294'967'295.0 * 2.0 - 1.0;
		value += 0.2 * noise * std::exp(-(time - duration / 2.0) * 40.0);
	}
	return static_cast<float>(value);
}

// Two overlapping notes, then both released before the end so the release stage is heard.
juce::MidiBuffer notesFor(int blockStart, int blockFrames)
{
	static const std::array<std::pair<int, juce::MidiMessage>, 4> events {
		std::pair { 0, juce::MidiMessage::noteOn(1, 45, 0.8f) },
		std::pair { 3'000, juce::MidiMessage::noteOn(1, 52, 0.6f) },
		std::pair { 6'000, juce::MidiMessage::noteOff(1, 45) },
		std::pair { 6'600, juce::MidiMessage::noteOff(1, 52) },
	};
	juce::MidiBuffer midi;
	for (const auto& [frame, message] : events)
		if (frame >= blockStart && frame < blockStart + blockFrames)
			midi.addEvent(message, frame - blockStart);
	return midi;
}

StereoAudio render(const RenderCase& renderCase)
{
	const auto& product = vekt::compat::product(renderCase.product);
	auto processor = product.create();
	for (const auto& [identifier, value] : renderCase.settings)
		setPlainValue(*processor, identifier, value);
	processor->setNonRealtime(renderCase.offline);
	processor->prepareToPlay(sampleRate, blockSize);

	StereoAudio output;
	output.sampleRate = sampleRate;
	for (auto& channel : output.channels) channel.resize(frames);
	juce::AudioBuffer<float> buffer(2, blockSize);
	for (auto start = 0; start < frames; start += blockSize)
	{
		const auto count = std::min(blockSize, frames - start);
		buffer.setSize(2, count, false, false, true);
		buffer.clear();
		if (!product.synth)
			for (auto channel = 0; channel < 2; ++channel)
				for (auto frame = 0; frame < count; ++frame)
					buffer.setSample(channel, frame, effectInput(channel, start + frame));
		auto midi = product.synth ? notesFor(start, count) : juce::MidiBuffer {};
		processor->processBlock(buffer, midi);
		for (auto channel = 0; channel < 2; ++channel)
			std::copy_n(buffer.getReadPointer(channel), count, output.channels[static_cast<std::size_t>(channel)].begin() + start);
	}
	processor->releaseResources();
	return output;
}

std::filesystem::path referencePath(const RenderCase& renderCase)
{
	return fixtureDirectory() / "audio" / renderCase.product / (renderCase.name + ".wav");
}

double rmsDb(const std::vector<float>& values)
{
	auto sum = 0.0;
	for (const auto value : values) sum += static_cast<double>(value) * value;
	return 10.0 * std::log10(std::max(sum / static_cast<double>(std::max<std::size_t>(values.size(), 1)), 1.0e-30));
}

struct Comparison
{
	float worst {};
	std::size_t worstFrame {};
	bool nonFinite {};
	double residualDb {};

	[[nodiscard]] bool matches() const noexcept { return !nonFinite && worst <= tolerance; }
};

// Non-finite output is tracked on its own: a NaN difference cannot be ranked against finite ones.
Comparison compare(const StereoAudio& reference, const StereoAudio& actual)
{
	Comparison result;
	std::vector<float> residual;
	std::vector<float> referenceSamples;
	for (std::size_t channel = 0; channel < 2; ++channel)
		for (std::size_t frame = 0; frame < actual.frames(); ++frame)
		{
			const auto expected = reference.channels[channel][frame];
			const auto sample = actual.channels[channel][frame];
			if (!std::isfinite(sample))
			{
				if (!result.nonFinite) result.worstFrame = frame;
				result.nonFinite = true;
				continue;
			}
			const auto difference = std::abs(sample - expected);
			if (difference > result.worst)
			{
				result.worst = difference;
				if (!result.nonFinite) result.worstFrame = frame; // the first non-finite frame is the one to report
			}
			residual.push_back(sample - expected);
			referenceSamples.push_back(expected);
		}
	result.residualDb = rmsDb(residual) - rmsDb(referenceSamples);
	return result;
}
}

static void checkReferences(const std::string& product)
{
	for (const auto& renderCase : renderCases())
	{
		if (renderCase.product != product) continue;
		const auto label = renderCase.product + "/" + renderCase.name;
		INFO(label);
		const auto reference = readWav(referencePath(renderCase));
		INFO("Missing or unreadable reference; capture with: vekt_dsp_tests \"[.capture-references]\"");
		REQUIRE(reference.has_value());

		const auto actual = render(renderCase);
		REQUIRE(reference->frames() == actual.frames());
		const auto comparison = compare(*reference, actual);
		if (!comparison.matches())
		{
			const auto saved = outputDirectory() / renderCase.product / (renderCase.name + ".wav");
			writeWav(saved, actual);
			FAIL_CHECK(label << " differs: " << (comparison.nonFinite ? "non-finite output, " : "") << "peak error "
				<< 20.0 * std::log10(std::max(comparison.worst, 1.0e-30f)) << " dBFS at frame " << comparison.worstFrame
				<< ", residual " << comparison.residualDb << " dB relative to the reference. This render: " << saved.string());
		}
	}
}

TEST_CASE("Reference comparison rejects non-finite output", "[compat][reference]")
{
	StereoAudio reference { sampleRate, { std::vector<float>(64, 0.25f), std::vector<float>(64, 0.25f) } };
	auto actual = reference;
	REQUIRE(compare(reference, actual).matches());
	actual.channels[1][10] = std::numeric_limits<float>::quiet_NaN(); // a transient NaN followed by matching samples
	const auto comparison = compare(reference, actual);
	REQUIRE(comparison.nonFinite);
	REQUIRE(comparison.worstFrame == 10);
	REQUIRE_FALSE(comparison.matches());
}

// One test per product so CTest can run them in parallel.
TEST_CASE("Every Rav reference render still sounds the same", "[compat][reference][rav]") { checkReferences("rav"); }
TEST_CASE("Every Glimmer reference render still sounds the same", "[compat][reference][glimmer]") { checkReferences("glimmer"); }
TEST_CASE("Every Kobber reference render still sounds the same", "[compat][reference][kobber]") { checkReferences("kobber"); }

TEST_CASE("Capture reference renders", "[.capture-references]")
{
	std::map<std::string, std::vector<std::pair<std::string, StereoAudio>>> byProduct;
	for (const auto& renderCase : renderCases())
	{
		const auto label = renderCase.product + "/" + renderCase.name;
		INFO(label);
		const auto first = render(renderCase);
		// A reference is only useful if the processor is deterministic for it.
		const auto deterministic = render(renderCase).channels == first.channels;
		REQUIRE(deterministic);
		const auto finite = std::ranges::all_of(first.channels, [](const auto& channel)
			{ return std::ranges::all_of(channel, [](const float sample) { return std::isfinite(sample); }); });
		REQUIRE(finite);
		REQUIRE(rmsDb(first.channels[0]) > -60.0);
		// Every case must exercise something the others do not.
		for (const auto& [otherName, other] : byProduct[renderCase.product])
		{
			INFO("renders identically to " << otherName);
			const auto distinct = other.channels != first.channels;
			REQUIRE(distinct);
		}
		byProduct[renderCase.product].emplace_back(renderCase.name, first);
		writeWav(referencePath(renderCase), first);
		std::cout << "Wrote " << referencePath(renderCase).string() << '\n';
	}
}
