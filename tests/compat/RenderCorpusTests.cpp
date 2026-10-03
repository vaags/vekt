// The render corpus scripts/render-diff.sh compares between two builds, byte for byte, to prove a change leaves the
// sound alone. Nothing here is stored: each case is rendered, checked for determinism and written as raw float32
// (left channel, then right) to $VEKT_RENDER_DIR. The script compiles this file into older revisions too, so it uses
// only the products' processors and their frozen parameter IDs. See docs/VERIFICATION_SPEED.md.

#include <vekt/flint/PluginProcessor.h>
#include <vekt/glimmer/PluginProcessor.h>
#include <vekt/kobber/PluginProcessor.h>
#include <vekt/rav/PluginProcessor.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

namespace
{
namespace kobber = vekt::kobber::parameters;
namespace rav = vekt::rav::parameters;
namespace glimmer = vekt::glimmer::parameters;
namespace flint = vekt::flint::parameters;

constexpr double sampleRate = 48'000.0;
constexpr int frames = 24'000; // 0.5 s

struct NoteEvent
{
	int frame;
	juce::MidiMessage message;
};

struct ParameterChange
{
	int frame; // applied at the start of the block holding it
	const char* identifier;
	float value;
};

struct CorpusCase
{
	std::string name; // also the output file name
	std::function<std::unique_ptr<juce::AudioProcessor>()> create;
	std::vector<std::pair<const char*, float>> settings; // plain parameter values; choices by index
	std::vector<NoteEvent> notes {}; // empty: an effect fed the fixed input below
	int blockSize { 256 };
	bool offline {};
	std::vector<ParameterChange> changes {};
	std::string sameAs {}; // a case this one must render identically to by design; otherwise every case is distinct
};

// Two overlapping notes released before the end, so attack, overlap and release are all heard.
std::vector<NoteEvent> twoNotes()
{
	return { { 0, juce::MidiMessage::noteOn(1, 45, 0.8f) }, { 6'000, juce::MidiMessage::noteOn(1, 52, 0.6f) },
		{ 12'000, juce::MidiMessage::noteOff(1, 45) }, { 15'000, juce::MidiMessage::noteOff(1, 52) } };
}

// Four overlapping notes, more than two voices hold, with releases in between: voice reuse and stealing.
std::vector<NoteEvent> fourNotes()
{
	return { { 0, juce::MidiMessage::noteOn(1, 45, 0.8f) }, { 3'000, juce::MidiMessage::noteOn(1, 52, 0.7f) },
		{ 6'000, juce::MidiMessage::noteOn(1, 57, 0.6f) }, { 8'000, juce::MidiMessage::noteOff(1, 52) },
		{ 9'000, juce::MidiMessage::noteOn(1, 60, 0.9f) }, { 14'000, juce::MidiMessage::noteOff(1, 45) },
		{ 16'000, juce::MidiMessage::noteOff(1, 57) }, { 18'000, juce::MidiMessage::noteOff(1, 60) } };
}

// A held low note under a higher one, released in the order that exercises priority and held-key return.
std::vector<NoteEvent> priorityNotes()
{
	return { { 0, juce::MidiMessage::noteOn(1, 52, 0.8f) }, { 4'000, juce::MidiMessage::noteOn(1, 45, 0.7f) },
		{ 8'000, juce::MidiMessage::noteOn(1, 57, 0.6f) }, { 12'000, juce::MidiMessage::noteOff(1, 57) },
		{ 15'000, juce::MidiMessage::noteOff(1, 45) }, { 18'000, juce::MidiMessage::noteOff(1, 52) } };
}

// Notes released while the sustain pedal is down, then the pedal lifted.
std::vector<NoteEvent> sustainedNotes()
{
	return { { 0, juce::MidiMessage::controllerEvent(1, 64, 127) }, { 0, juce::MidiMessage::noteOn(1, 45, 0.8f) },
		{ 3'000, juce::MidiMessage::noteOn(1, 52, 0.6f) }, { 6'000, juce::MidiMessage::noteOff(1, 45) },
		{ 8'000, juce::MidiMessage::noteOff(1, 52) }, { 14'000, juce::MidiMessage::controllerEvent(1, 64, 0) } };
}

// Poly controllers: pedal, poly aftertouch, channel pressure and bend, Reset All Controllers releasing the pedal's
// notes mid-phrase, then All Notes Off and All Sound Off.
std::vector<NoteEvent> controllerNotes()
{
	return { { 0, juce::MidiMessage::controllerEvent(1, 64, 127) }, { 0, juce::MidiMessage::noteOn(1, 48, 0.8f) },
		{ 2'000, juce::MidiMessage::noteOn(1, 55, 0.7f) }, { 3'000, juce::MidiMessage::aftertouchChange(1, 55, 100) },
		{ 4'000, juce::MidiMessage::channelPressureChange(1, 80) }, { 5'000, juce::MidiMessage::pitchWheel(1, 14'000) },
		{ 6'000, juce::MidiMessage::noteOff(1, 48) }, { 9'000, juce::MidiMessage::controllerEvent(1, 121, 0) },
		{ 11'000, juce::MidiMessage::noteOn(1, 60, 0.8f) }, { 15'000, juce::MidiMessage::allNotesOff(1) },
		{ 17'000, juce::MidiMessage::noteOn(1, 62, 0.8f) }, { 21'000, juce::MidiMessage::allSoundOff(1) } };
}

// A monophonic phrase on channel 2 with the pedal down: held-key return, then a sustained release.
std::vector<NoteEvent> channelTwoNotes()
{
	return { { 0, juce::MidiMessage::controllerEvent(2, 64, 127) }, { 0, juce::MidiMessage::noteOn(2, 45, 0.8f) },
		{ 4'000, juce::MidiMessage::noteOn(2, 52, 0.7f) }, { 8'000, juce::MidiMessage::noteOff(2, 52) },
		{ 12'000, juce::MidiMessage::noteOff(2, 45) }, { 16'000, juce::MidiMessage::controllerEvent(2, 64, 0) } };
}

// Flint hits: soft, then two louder ones, one with a note off while it rings.
std::vector<NoteEvent> hits()
{
	return { { 0, juce::MidiMessage::noteOn(1, 36, 0.5f) }, { 8'000, juce::MidiMessage::noteOn(1, 36, 0.9f) },
		{ 10'000, juce::MidiMessage::noteOff(1, 36) }, { 16'000, juce::MidiMessage::noteOn(1, 36, 1.0f) } };
}

// One held note under pitch bend and the mod wheel (vibrato).
std::vector<NoteEvent> expressiveNote()
{
	return { { 0, juce::MidiMessage::noteOn(1, 48, 0.8f) }, { 4'000, juce::MidiMessage::pitchWheel(1, 12'000) },
		{ 8'000, juce::MidiMessage::controllerEvent(1, 1, 100) }, { 12'000, juce::MidiMessage::pitchWheel(1, 4'000) },
		{ 18'000, juce::MidiMessage::noteOff(1, 48) } };
}

std::vector<CorpusCase> corpus()
{
	const auto ravProcessor = [] { return std::make_unique<vekt::rav::PluginProcessor>(); };
	const auto glimmerProcessor = [] { return std::make_unique<vekt::glimmer::PluginProcessor>(); };
	const auto kobberProcessor = [] { return std::make_unique<vekt::kobber::PluginProcessor>(); };
	const auto flintProcessor = [] { return std::make_unique<vekt::flint::PluginProcessor>(); };
	std::vector<CorpusCase> cases {
		{ "rav-default", ravProcessor, {} },
		{ "rav-overdrive", ravProcessor, { { rav::mode, 1.0f }, { rav::drive, 18.0f } } },
		{ "rav-distortion", ravProcessor, { { rav::mode, 2.0f }, { rav::drive, 18.0f } } },
		{ "rav-circuit-fuzz", ravProcessor, { { rav::mode, 3.0f }, { rav::drive, 18.0f } } },
		{ "rav-gated-fuzz", ravProcessor, { { rav::mode, 4.0f }, { rav::drive, 18.0f } } },
		{ "rav-multiband", ravProcessor,
		    { { rav::lowBandMix, 30.0f }, { rav::highBandMix, 80.0f }, { rav::lowMidCutoffHz, 300.0f },
		        { rav::tone, 0.6f }, { rav::mix, 70.0f }, { rav::bias, 0.3f } } },
		{ "rav-16x-fir", ravProcessor, { { rav::trackingOversampling, 6.0f } } },
		{ "rav-offline-2x-fir", ravProcessor,
		    { { rav::trackingOversampling, 0.0f }, { rav::offlineOversampling, 1.0f } }, {}, 256, true },
		{ "glimmer-default", glimmerProcessor, {} },
		{ "glimmer-drum", glimmerProcessor, { { glimmer::cabinetModel, 1.0f } } },
		{ "glimmer-wide-fast", glimmerProcessor, { { glimmer::cabinetModel, 2.0f }, { glimmer::speedMode, 1.0f } } },
		{ "glimmer-auto", glimmerProcessor, { { glimmer::speedMode, 2.0f } } },
		{ "glimmer-brake-driven", glimmerProcessor, { { glimmer::brake, 1.0f }, { glimmer::preampDrive, 18.0f } } },
		{ "glimmer-offline-2x-fir", glimmerProcessor,
		    { { glimmer::trackingOversampling, 0.0f }, { glimmer::offlineOversampling, 1.0f } }, {}, 256, true },
		{ "kobber-ladder", kobberProcessor, {}, twoNotes() },
		{ "kobber-ladder-4x-fir", kobberProcessor, { { kobber::trackingOversampling, 4.0f } }, twoNotes() },
		{ "kobber-ladder-hp", kobberProcessor, { { kobber::filterMode, 1.0f }, { kobber::filterResonance, 60.0f } },
		    twoNotes() },
		{ "kobber-svf-2x-iir", kobberProcessor,
		    { { kobber::filterType, 1.0f }, { kobber::trackingOversampling, 1.0f }, { kobber::filterMode, 0.0f },
		        { kobber::filterResonance, 50.0f } },
		    twoNotes() },
		{ "kobber-k35-8x-fir", kobberProcessor,
		    { { kobber::filterType, 2.0f }, { kobber::trackingOversampling, 5.0f }, { kobber::filterResonance, 60.0f } },
		    twoNotes() },
		{ "kobber-osc-ranges", kobberProcessor,
		    { { kobber::osc2Range, 0.0f }, { kobber::osc2Level, 60.0f }, { kobber::osc3Range, 4.0f },
		        { kobber::osc3Level, 40.0f } },
		    twoNotes() },
		{ "kobber-white-noise", kobberProcessor, { { kobber::noiseType, 1.0f }, { kobber::noiseLevel, 40.0f } }, twoNotes() },
		{ "kobber-pink-noise", kobberProcessor, { { kobber::noiseType, 2.0f }, { kobber::noiseLevel, 40.0f } }, twoNotes() },
		// Mono's startup sound is unison 2x at 12 cents, Mono Legato: the cases set what differs from it.
		{ "kobber-unison-off", kobberProcessor, { { kobber::unison, 0.0f } }, twoNotes() },
		{ "kobber-unison-4x-lfo", kobberProcessor,
		    { { kobber::unison, 2.0f }, { kobber::lfos[0].rate, 6.0f }, { kobber::lfos[0].pitch[0], 2.0f },
		        { kobber::lfos[0].delay, 0.0f }, { kobber::lfos[0].fade, 0.0f } },
		    twoNotes() },
		{ "kobber-poly", kobberProcessor, { { kobber::performanceMode, 0.0f } }, fourNotes() },
		{ "kobber-voice-stealing", kobberProcessor, { { kobber::performanceMode, 0.0f }, { kobber::voiceCount, 0.0f } },
		    fourNotes() },
		{ "kobber-poly-sustain", kobberProcessor, { { kobber::performanceMode, 0.0f } }, sustainedNotes() },
		{ "kobber-last-priority", kobberProcessor, { { kobber::performanceMode, 1.0f } }, priorityNotes() },
		{ "kobber-low-priority", kobberProcessor, { { kobber::performanceMode, 1.0f }, { kobber::notePriority, 1.0f } },
		    priorityNotes() },
		{ "kobber-legato-glide", kobberProcessor,
		    { { kobber::performanceMode, 2.0f }, { kobber::glideMode, 2.0f }, { kobber::glideTime, 0.05f } },
		    priorityNotes() },
		{ "kobber-no-held-key-return", kobberProcessor, { { kobber::performanceMode, 1.0f }, { kobber::heldKeyReturn, 0.0f } },
		    priorityNotes() },
		{ "kobber-glide-always", kobberProcessor, { { kobber::glideMode, 1.0f }, { kobber::glideTime, 0.08f } }, fourNotes() },
		{ "kobber-mono-sustain", kobberProcessor, { { kobber::performanceMode, 1.0f } }, sustainedNotes() },
		{ "kobber-bend-vibrato", kobberProcessor, { { kobber::vibratoDepth, 50.0f }, { kobber::vibratoRate, 5.0f } },
		    expressiveNote() },
		// Multicore changes threads, not samples.
		{ "kobber-multicore", kobberProcessor, { { kobber::performanceMode, 0.0f }, { kobber::multicore, 1.0f } }, fourNotes(),
		    256, false, {}, "kobber-poly" },
		{ "kobber-multicore-unison-off", kobberProcessor,
		    { { kobber::performanceMode, 0.0f }, { kobber::unison, 0.0f }, { kobber::multicore, 1.0f } }, fourNotes() },
		{ "kobber-controllers", kobberProcessor,
		    { { kobber::performanceMode, 0.0f }, { kobber::pitchBendRange, 12.0f }, { kobber::vibratoDepth, 40.0f } },
		    controllerNotes() },
		{ "kobber-channel-2-sustain", kobberProcessor, { { kobber::performanceMode, 1.0f } }, channelTwoNotes() },
		// The startup sound delays and fades its LFOs in: the LFO cases start them at once.
		{ "kobber-lfo-sync", kobberProcessor,
		    { { kobber::lfos[0].sync, 1.0f }, { kobber::lfos[0].pitch[0], 2.0f }, { kobber::lfos[0].delay, 0.0f },
		        { kobber::lfos[0].fade, 0.0f } },
		    twoNotes() },
		// A parameter change between a note and the held-key return to an earlier one.
		{ "kobber-change-before-return", kobberProcessor, { { kobber::performanceMode, 1.0f } }, priorityNotes(), 256, false,
		    { { 10'000, kobber::filterCutoff, 500.0f } } },
		{ "kobber-odd-blocks", kobberProcessor,
		    { { kobber::performanceMode, 0.0f }, { kobber::unison, 2.0f }, { kobber::unisonDetune, 20.0f } }, fourNotes(),
		    127 },
		{ "kobber-offline-8x-fir", kobberProcessor, { { kobber::offlineOversampling, 3.0f } }, twoNotes(), 256, true },
		// Flint: Kick / Classic Analog with each Drive Type, Mallet / Bar, and a Mode change between hits.
		{ "flint-kick", flintProcessor, { { flint::variation, 0.0f } }, hits() },
		{ "flint-kick-hard-drive", flintProcessor,
		    { { flint::variation, 0.0f }, { flint::drive, 60.0f }, { flint::driveType, 1.0f } }, hits() },
		{ "flint-kick-fold-drive", flintProcessor,
		    { { flint::variation, 0.0f }, { flint::drive, 60.0f }, { flint::driveType, 2.0f } }, hits() },
		{ "flint-mallet", flintProcessor,
		    { { flint::variation, 0.0f }, { flint::mode, 7.0f }, { flint::pitch, 72.0f } }, hits() },
		// Variation stays 0: each instance draws its own seed (kept in the project), so hits would differ between runs.
		{ "flint-kick-sweep-click", flintProcessor,
		    { { flint::variation, 0.0f }, { flint::kickSweep, 50.0f }, { flint::kickClick, 50.0f } }, hits() },
		{ "flint-mode-change", flintProcessor, { { flint::variation, 0.0f } }, hits(), 256, false,
		    { { 12'000, flint::mode, 7.0f } } },
	};
	return cases;
}

// The effects' input: a sweep from 80 Hz to 6 kHz with a decaying noise burst halfway, slightly different per channel.
float effectInput(int channel, int frame)
{
	const auto time = frame / sampleRate;
	const auto duration = frames / sampleRate;
	const auto ratio = 6'000.0 / 80.0;
	const auto phase =
	    2.0 * std::numbers::pi * 80.0 * duration / std::log(ratio) * (std::pow(ratio, time / duration) - 1.0);
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

void setPlainValue(juce::AudioProcessor& processor, const char* identifier, float value)
{
	for (auto* parameter : processor.getParameters())
		if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter);
		    ranged != nullptr && ranged->paramID == identifier)
		{
			ranged->setValueNotifyingHost(ranged->convertTo0to1(value));
			return;
		}
	FAIL("No parameter " << identifier);
}

std::array<std::vector<float>, 2> render(const CorpusCase& corpusCase)
{
	auto processor = corpusCase.create();
	for (const auto& [identifier, value] : corpusCase.settings) setPlainValue(*processor, identifier, value);
	processor->setNonRealtime(corpusCase.offline);
	processor->prepareToPlay(sampleRate, corpusCase.blockSize);
	const auto synth = !corpusCase.notes.empty();
	std::array<std::vector<float>, 2> output;
	for (auto& channel : output) channel.resize(frames);
	juce::AudioBuffer<float> buffer(2, corpusCase.blockSize);
	for (auto start = 0; start < frames; start += corpusCase.blockSize)
	{
		const auto count = std::min(corpusCase.blockSize, frames - start);
		buffer.setSize(2, count, false, false, true);
		buffer.clear();
		for (const auto& change : corpusCase.changes)
			if (change.frame >= start && change.frame < start + count)
				setPlainValue(*processor, change.identifier, change.value);
		juce::MidiBuffer midi;
		if (synth)
		{
			for (const auto& [frame, message] : corpusCase.notes)
				if (frame >= start && frame < start + count) midi.addEvent(message, frame - start);
		}
		else
		{
			for (auto channel = 0; channel < 2; ++channel)
				for (auto frame = 0; frame < count; ++frame)
					buffer.setSample(channel, frame, effectInput(channel, start + frame));
		}
		processor->processBlock(buffer, midi);
		for (auto channel = 0; channel < 2; ++channel)
			std::copy_n(
			    buffer.getReadPointer(channel), count, output[static_cast<std::size_t>(channel)].begin() + start);
	}
	processor->releaseResources();
	return output;
}
}

TEST_CASE("Render the corpus for render-diff", "[.render-corpus]")
{
	const auto* directory = std::getenv("VEKT_RENDER_DIR");
	if (directory == nullptr)
		FAIL("Set VEKT_RENDER_DIR to the directory the renders go to (scripts/render-diff.sh does)");
	const std::filesystem::path outputDirectory { directory };
	std::filesystem::create_directories(outputDirectory);
	const auto cases = corpus();
	std::vector<std::array<std::vector<float>, 2>> renders;
	for (const auto& corpusCase : cases)
	{
		INFO(corpusCase.name);
		const auto& first = renders.emplace_back(render(corpusCase));
		// A byte comparison is only meaningful if the build renders the case the same way every time, and only
		// covers something if the case is audible.
		REQUIRE(render(corpusCase) == first);
		REQUIRE(std::ranges::all_of(first, [](const auto& channel)
		    { return std::ranges::all_of(channel, [](const float sample) { return std::isfinite(sample); }); }));
		auto energy = 0.0;
		for (const auto sample : first[0]) energy += static_cast<double>(sample) * sample;
		REQUIRE(10.0 * std::log10(std::max(energy / frames, 1.0e-30)) > -60.0);
		std::ofstream file(outputDirectory / (corpusCase.name + ".f32"), std::ios::binary);
		for (const auto& channel : first)
			file.write(reinterpret_cast<const char*>(channel.data()),
			    static_cast<std::streamsize>(channel.size() * sizeof(float)));
		file.close();
		REQUIRE_FALSE(file.fail());
	}
	// Every case must exercise something the others do not, unless it declares the case it renders identically to.
	for (std::size_t first = 0; first < cases.size(); ++first)
		for (std::size_t second = first + 1; second < cases.size(); ++second)
		{
			INFO(cases[first].name << " and " << cases[second].name);
			const auto declared =
			    cases[first].sameAs == cases[second].name || cases[second].sameAs == cases[first].name;
			REQUIRE((renders[first] == renders[second]) == declared);
		}
}
