#include <vekt/flint/Parameters.h>
#include <vekt/flint/PluginProcessor.h>

#include <juce_audio_formats/juce_audio_formats.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <functional>
#include <map>
#include <memory>

namespace
{
namespace parameters = vekt::flint::parameters;
constexpr double sampleRate = 48'000.0;
constexpr int blockSize = 256;

void setParameter(vekt::flint::PluginProcessor& processor, const char* identifier, float value)
{
	auto* parameter = processor.getParameters().getParameter(identifier);
	REQUIRE(parameter != nullptr);
	parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

// A score: notes at sample positions (velocity 0 is a Note Off) and parameter changes at block starts.
struct Score
{
	std::multimap<long long, std::pair<int, float>> notes;
	std::multimap<long long, std::function<void(vekt::flint::PluginProcessor&)>> changes;
	double seconds {};
};

juce::AudioBuffer<float> perform(
    int program, const Score& score, const std::function<void(vekt::flint::PluginProcessor&)>& setup = {})
{
	auto processor = std::make_unique<vekt::flint::PluginProcessor>();
	processor->setCurrentProgram(program);
	if (setup) setup(*processor);
	processor->prepareToPlay(sampleRate, blockSize);
	const auto length = static_cast<int>(score.seconds * sampleRate);
	juce::AudioBuffer<float> output(2, length);
	juce::AudioBuffer<float> block(2, blockSize);
	juce::MidiBuffer midi;
	for (long long start = 0; start < length; start += blockSize)
	{
		for (auto change = score.changes.lower_bound(start);
		    change != score.changes.end() && change->first < start + blockSize; ++change)
			change->second(*processor);
		midi.clear();
		for (auto note = score.notes.lower_bound(start); note != score.notes.end() && note->first < start + blockSize;
		    ++note)
		{
			const auto offset = static_cast<int>(note->first - start);
			const auto [number, velocity] = note->second;
			midi.addEvent(velocity > 0.0f ? juce::MidiMessage::noteOn(1, number, velocity)
			                              : juce::MidiMessage::noteOff(1, number),
			    offset);
		}
		processor->processBlock(block, midi);
		const auto count = static_cast<int>(std::min<long long>(blockSize, length - start));
		for (auto channel = 0; channel < 2; ++channel)
			output.copyFrom(channel, static_cast<int>(start), block, channel, 0, count);
	}
	return output;
}

long long at(double seconds) { return static_cast<long long>(seconds * sampleRate); }

void write(const juce::File& folder, const juce::String& name, const juce::AudioBuffer<float>& audio)
{
	const auto file = folder.getChildFile(name + ".wav");
	file.deleteFile();
	std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
	REQUIRE(stream != nullptr);
	juce::WavAudioFormat format;
	auto writer = format.createWriterFor(
	    stream, juce::AudioFormatWriterOptions {}.withSampleRate(sampleRate).withNumChannels(2).withBitsPerSample(32));
	REQUIRE(writer != nullptr);
	REQUIRE(writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples()));
}
}

// Listening renders for A22 (docs/FLINT_VALIDATION.md), hidden: with VEKT_FLINT_AUDITION set to a folder, 48 kHz
// stereo WAVs of each listening case. Factory programs: 0 Classic, 1 Long Boom, 3 Distorted Sub, 4 Rosewood Marimba,
// 5 Xylophone, 6 Vibraphone.
TEST_CASE("Flint audition renders", "[.][flint-audition]")
{
	const auto* path = std::getenv("VEKT_FLINT_AUDITION");
	if (path == nullptr) SKIP("VEKT_FLINT_AUDITION is not set");
	const juce::File folder(juce::String { path });
	REQUIRE(folder.createDirectory().wasOk());

	Score sixteenths { {}, {}, 4.5 };
	for (auto step = 0; step < 32; ++step)
		sixteenths.notes.insert({ at(step * 0.125), { 36, step % 4 == 0 ? 1.0f : 0.7f } });
	write(folder, "01-kick-16ths", perform(0, sixteenths));

	Score doubles { {}, {}, 4.0 };
	for (auto beat = 0; beat < 6; ++beat)
		for (const auto offset : { 0.0, 0.03 }) doubles.notes.insert({ at(beat * 0.5 + offset), { 36, 0.9f } });
	write(folder, "02-kick-fast-doubles", perform(1, doubles));

	Score roll { {}, {}, 5.0 };
	for (auto hit = 0; hit < 40; ++hit)
		roll.notes.insert({ at(hit * 0.05), { 60, 0.6f + 0.3f * static_cast<float>(hit % 2) } });
	write(folder, "03-bar-roll-on-long-tail", perform(6, roll));
	write(folder, "04-bar-repeated-strikes", perform(4, roll));

	Score glide { {}, {}, 4.0 };
	glide.notes.insert({ 0, { 60, 1.0f } });
	glide.changes.insert({ at(1.0), [](auto& processor) { setParameter(processor, parameters::pitch, 60.0f); } });
	glide.changes.insert({ at(2.0), [](auto& processor) { setParameter(processor, parameters::pitch, 55.0f); } });
	write(folder, "05-bar-pitch-glide-over-tail", perform(6, glide));
	Score kickGlide = glide;
	kickGlide.changes.clear();
	kickGlide.changes.insert({ at(0.5), [](auto& processor) { setParameter(processor, parameters::pitch, 38.0f); } });
	write(folder, "06-kick-pitch-glide-over-tail", perform(1, kickGlide));

	Score damped { {}, {}, 4.0 };
	for (auto beat = 0; beat < 4; ++beat)
	{
		damped.notes.insert({ at(beat * 1.0), { 36, 1.0f } });
		damped.notes.insert({ at(beat * 1.0 + 0.15 * (beat + 1)), { 36, 0.0f } });
	}
	write(folder, "07-kick-note-off-damps",
	    perform(1, damped, [](auto& processor) { setParameter(processor, parameters::noteOffDamps, 1.0f); }));
	write(folder, "08-bar-note-off-damps",
	    perform(6, damped, [](auto& processor) { setParameter(processor, parameters::noteOffDamps, 1.0f); }));

	Score eight { {}, {}, 2.5 };
	for (auto hit = 0; hit < 8; ++hit) eight.notes.insert({ at(hit * 0.25), { 60, 1.0f } });
	for (const auto variation : { 0.0f, 30.0f, 100.0f })
	{
		const auto suffix = juce::String(static_cast<int>(variation));
		const auto clicky = [variation](auto& processor)
		{
			setParameter(processor, parameters::variation, variation);
			setParameter(processor, parameters::kickClick, 60.0f);
			setParameter(processor, parameters::kickSweep, 30.0f);
		};
		write(folder, "09-kick-variation-" + suffix, perform(0, eight, clicky));
		write(folder, "10-bar-variation-" + suffix,
		    perform(
		        5, eight, [variation](auto& processor) { setParameter(processor, parameters::variation, variation); }));
	}

	for (const auto type : { 0, 1, 2 })
		write(folder, "11-kick-drive-" + juce::String(juce::StringArray { "soft", "hard", "fold" }[type]),
		    perform(3, eight,
		        [type](auto& processor) { setParameter(processor, parameters::driveType, static_cast<float>(type)); }));

	Score scale { {}, {}, 3.0 };
	for (auto hit = 0; hit < 8; ++hit) scale.notes.insert({ at(hit * 0.3), { 60, 0.9f } });
	for (const auto overtones : { 0.0f, 50.0f, 100.0f })
		for (const auto resonator : { 0.0f, 50.0f })
			write(folder,
			    "12-bar-overtones-" + juce::String(static_cast<int>(overtones)) + "-resonator-" +
			        juce::String(static_cast<int>(resonator)),
			    perform(4, scale,
			        [overtones, resonator](auto& processor)
			        {
				        setParameter(processor, parameters::barOvertones, overtones);
				        setParameter(processor, parameters::barResonator, resonator);
			        }));
}
