#include <vekt/kobber/PluginProcessor.h>

#include "KobberQualitySweep.h"
#include "FilterPrototypeSupport.h"
#include "KobberVoice.h"

#include <vekt/presets/PresetSchema.h>

#include <juce_audio_formats/juce_audio_formats.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <numbers>
#include <vector>

namespace
{
namespace parameters = vekt::kobber::parameters;

void setParameter(vekt::kobber::PluginProcessor& processor, const char* identifier, float value)
{
	auto* parameter = processor.getParameters().getParameter(identifier);
	parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

// Holds `note` (or plays nothing if note < 0) for `blocks` 512-sample blocks at 48 kHz; beforeBlock runs before each
// block, so parameter changes land on block boundaries.
juce::AudioBuffer<float> hold(vekt::kobber::PluginProcessor& processor, int blocks, int note,
	const std::function<void(int)>& beforeBlock = {})
{
	juce::AudioBuffer<float> output(2, blocks * 512);
	juce::AudioBuffer<float> block(2, 512);
	for (int index = 0; index < blocks; ++index)
	{
		if (beforeBlock) beforeBlock(index);
		juce::MidiBuffer midi;
		if (index == 0 && note >= 0) midi.addEvent(juce::MidiMessage::noteOn(1, note, 0.8f), 0);
		block.clear();
		processor.processBlock(block, midi);
		for (int channel = 0; channel < 2; ++channel) output.copyFrom(channel, index * 512, block, channel, 0, 512);
	}
	return output;
}

// Largest sample-to-sample step of the left channel over [start, end).
float largestStep(const juce::AudioBuffer<float>& buffer, int start, int end)
{
	float step {};
	for (int sample = std::max(1, start); sample < end; ++sample)
		step = std::max(step, std::abs(buffer.getSample(0, sample) - buffer.getSample(0, sample - 1)));
	return step;
}

bool identical(const juce::AudioBuffer<float>& first, const juce::AudioBuffer<float>& second)
{
	for (int channel = 0; channel < 2; ++channel)
		for (int sample = 0; sample < first.getNumSamples(); ++sample)
			if (!juce::exactlyEqual(first.getSample(channel, sample), second.getSample(channel, sample))) return false;
	return true;
}

// Plays a three-note chord for most of `samples` in 512-sample blocks, then releases it.
juce::AudioBuffer<float> playChord(vekt::kobber::PluginProcessor& processor, int samples)
{
	juce::AudioBuffer<float> output(2, samples);
	juce::AudioBuffer<float> block(2, 512);
	for (int start = 0; start < samples; start += 512)
	{
		juce::MidiBuffer midi;
		for (const auto note : { 45, 52, 61 })
		{
			if (start == 0) midi.addEvent(juce::MidiMessage::noteOn(1, note, 0.8f), 0);
			if (start == samples * 3 / 4 / 512 * 512) midi.addEvent(juce::MidiMessage::noteOff(1, note), 100);
		}
		block.clear();
		processor.processBlock(block, midi);
		for (int channel = 0; channel < 2; ++channel)
			output.copyFrom(channel, start, block, channel, 0, std::min(512, samples - start));
	}
	return output;
}

// Representative filter fixtures: the default patch, a hot driven notch patch with every ladder option and filter
// modulation on, and a self-oscillating low-cutoff patch.
void applyFixture(vekt::kobber::PluginProcessor& processor, int fixture)
{
	if (fixture == 1)
	{
		setParameter(processor, parameters::unison, 2.0f);
		setParameter(processor, parameters::filterDrive, 18.0f);
		setParameter(processor, parameters::filterResonance, 90.0f);
		setParameter(processor, parameters::filterMode, 0.3f);
		setParameter(processor, parameters::filterQCompensation, 1.0f);
		setParameter(processor, parameters::lfos[0].rate, 3.0f);
		setParameter(processor, parameters::lfos[0].filterMode, 40.0f);
		setParameter(processor, parameters::lfos[0].filter, 1.0f);
		setParameter(processor, parameters::drift, 50.0f);
		setParameter(processor, parameters::noiseType, 2.0f);
		setParameter(processor, parameters::noiseLevel, 30.0f);
	}
	if (fixture == 2)
	{
		setParameter(processor, parameters::filterCutoff, 200.0f);
		setParameter(processor, parameters::filterResonance, 100.0f);
		setParameter(processor, parameters::filterEnvelopeAmount, 80.0f);
	}
}
}

// Development check, hidden from normal runs: with VEKT_KOBBER_DUMP set to a directory, writes raw renders of the
// fixtures at several rates and qualities, to compare two builds bit for bit.
TEST_CASE("Kobber dumps filter fixture renders", "[.][kobber-dump]")
{
	const auto* directory = std::getenv("VEKT_KOBBER_DUMP");
	REQUIRE(directory != nullptr);
	const juce::File folder(directory);
	REQUIRE(folder.createDirectory().wasOk());
	struct Case { int fixture, quality; double sampleRate; bool multicore, svf, k35 = false; };
	for (const auto& [fixture, quality, sampleRate, multicore, svf, k35] : { Case { 0, 0, 48'000.0, false, false }, Case { 1, 0, 48'000.0, false, false },
		Case { 2, 0, 48'000.0, false, false }, Case { 0, 1, 48'000.0, false, false }, Case { 1, 1, 44'100.0, false, false },
		Case { 2, 1, 96'000.0, false, false }, Case { 1, 3, 48'000.0, false, false }, Case { 1, 0, 48'000.0, true, false },
		Case { 0, 0, 48'000.0, false, true }, Case { 1, 1, 44'100.0, false, true }, Case { 2, 1, 96'000.0, false, true },
		Case { 1, 3, 48'000.0, false, true }, Case { 1, 0, 48'000.0, true, true }, Case { 0, 0, 48'000.0, false, false, true },
		Case { 1, 1, 44'100.0, false, false, true }, Case { 2, 1, 96'000.0, false, false, true }, Case { 1, 3, 48'000.0, false, false, true },
		Case { 1, 0, 48'000.0, true, false, true } })
	{
		vekt::kobber::PluginProcessor processor;
		setParameter(processor, parameters::trackingOversampling, kobberQualitySweep[static_cast<std::size_t>(quality)]);
		setParameter(processor, parameters::multicore, multicore ? 1.0f : 0.0f);
		setParameter(processor, parameters::filterType, k35 ? 2.0f : svf ? 1.0f : 0.0f);
		applyFixture(processor, fixture);
		processor.prepareToPlay(sampleRate, 512);
		const auto render = playChord(processor, 48 * 512);
		const auto name = "f" + juce::String(fixture) + "-q" + juce::String(quality) + "-" + juce::String(sampleRate, 0)
			+ (multicore ? "-mc" : "") + (svf ? "-svf" : "") + (k35 ? "-k35" : "") + ".raw";
		juce::FileOutputStream stream(folder.getChildFile(name));
		REQUIRE(stream.openedOk());
		stream.setPosition(0);
		stream.truncate();
		for (int channel = 0; channel < 2; ++channel)
			stream.write(render.getReadPointer(channel), static_cast<std::size_t>(render.getNumSamples()) * sizeof(float));
		CHECK(render.getRMSLevel(0, 0, render.getNumSamples()) > 1.0e-3f);
	}
}

TEST_CASE("Kobber filter type offers Ladder, SVF and K35 and defaults to Ladder", "[kobber][filter][filter-type][parameters]")
{
	vekt::kobber::PluginProcessor processor;
	auto* parameter = dynamic_cast<juce::AudioParameterChoice*>(processor.getParameters().getParameter(parameters::filterType));
	REQUIRE(parameter != nullptr);
	REQUIRE(parameter->choices == juce::StringArray { "Ladder", "SVF", "K35" });
	REQUIRE(parameter->getIndex() == 0);
}

TEST_CASE("Kobber SVF renders its own finite sound", "[kobber][filter][filter-type]")
{
	const auto render = [](float type)
	{
		vekt::kobber::PluginProcessor processor;
		setParameter(processor, parameters::filterType, type);
		setParameter(processor, parameters::filterResonance, 60.0f);
		processor.prepareToPlay(48'000.0, 512);
		return hold(processor, 24, 45);
	};
	const auto ladder = render(0.0f);
	const auto svf = render(1.0f);
	const auto samples = svf.getNumSamples();
	for (int channel = 0; channel < 2; ++channel)
		for (int sample = 0; sample < samples; ++sample) REQUIRE(std::isfinite(svf.getSample(channel, sample)));
	REQUIRE(svf.getRMSLevel(0, 0, samples) > 0.01f);
	REQUIRE_FALSE(identical(ladder, svf));
}

TEST_CASE("Kobber voices adopt a filter type chosen while silent without a transition", "[kobber][filter][filter-type]")
{
	vekt::kobber::PluginProcessor fromStart;
	setParameter(fromStart, parameters::filterType, 1.0f);
	fromStart.prepareToPlay(48'000.0, 512);
	hold(fromStart, 4, -1);
	const auto expected = hold(fromStart, 16, 45);

	vekt::kobber::PluginProcessor switched;
	switched.prepareToPlay(48'000.0, 512);
	hold(switched, 4, -1, [&switched](int block) { if (block == 2) setParameter(switched, parameters::filterType, 1.0f); });
	REQUIRE(identical(hold(switched, 16, 45), expected));
}

TEST_CASE("Kobber filter type switches under a held note without a click", "[kobber][filter][filter-type]")
{
	for (const auto [from, to] : { std::pair { 0.0f, 1.0f }, std::pair { 1.0f, 0.0f } })
	{
		INFO("from " << from << " to " << to);
		vekt::kobber::PluginProcessor processor;
		setParameter(processor, parameters::filterType, from);
		setParameter(processor, parameters::filterCutoff, 1'500.0f);
		setParameter(processor, parameters::filterResonance, 30.0f);
		processor.prepareToPlay(48'000.0, 512);
		constexpr int switchBlock = 24;
		const auto output = hold(processor, 48, 45, [&processor, to](int block)
		{
			if (block == switchBlock) setParameter(processor, parameters::filterType, to);
		});
		constexpr auto switchSample = switchBlock * 512;
		// The steady sound on either side sets the scale; the switch itself must not step further than it.
		const auto steady = std::max(largestStep(output, switchSample - 4'096, switchSample),
			largestStep(output, switchSample + 4'096, switchSample + 8'192));
		const auto atSwitch = largestStep(output, switchSample - 1, switchSample + 256);
		INFO("steady " << steady << ", at switch " << atSwitch);
		REQUIRE(steady > 0.0f);
		REQUIRE(atSwitch <= 1.25f * steady);
	}
}

namespace
{
// SVF and K35 read the same smoothed Mode as the ladder: a Mode change ramps from the previous response, then the held
// sound has changed substantially.
void checkModeSweep(float filterType)
{
	vekt::kobber::PluginProcessor lowPass, swept;
	for (auto* processor : { &lowPass, &swept })
	{
		setParameter(*processor, parameters::filterType, filterType);
		setParameter(*processor, parameters::filterCutoff, 800.0f);
		setParameter(*processor, parameters::filterResonance, 60.0f);
		processor->prepareToPlay(48'000.0, 512);
	}
	constexpr int changeBlock = 8;
	for (const auto mode : { 0.0f, 1.0f })
	{
		INFO("mode " << mode);
		const auto reference = hold(lowPass, 16, 48);
		const auto output = hold(swept, 16, 48, [&swept, mode](int block)
		{
			if (block == changeBlock) setParameter(swept, parameters::filterMode, mode);
			if (block == 0) setParameter(swept, parameters::filterMode, -1.0f);
		});
		constexpr auto changeSample = changeBlock * 512;
		REQUIRE(std::abs(output.getSample(0, changeSample) - reference.getSample(0, changeSample)) < 0.05f);
		// Steady after the 20 ms ramp: the sound has changed substantially.
		double difference {}, level {};
		for (int sample = changeSample + 2'048; sample < output.getNumSamples(); ++sample)
		{
			const auto delta = static_cast<double>(output.getSample(0, sample) - reference.getSample(0, sample));
			difference += delta * delta;
			level += static_cast<double>(reference.getSample(0, sample)) * reference.getSample(0, sample);
		}
		REQUIRE(difference > 0.01 * level);
		for (int sample = 0; sample < output.getNumSamples(); ++sample) REQUIRE(std::isfinite(output.getSample(0, sample)));
		// Release and let both processors fall silent before the next pass.
		for (auto* processor : { &lowPass, &swept })
		{
			juce::AudioBuffer<float> block(2, 512);
			juce::MidiBuffer off;
			off.addEvent(juce::MidiMessage::allNotesOff(1), 0);
			for (int index = 0; index < 400; ++index) { processor->processBlock(block, off); off.clear(); }
		}
	}
}
}

TEST_CASE("Kobber SVF Mode sweeps smoothly and changes the held sound", "[kobber][filter][filter-type][svf-mode]") { checkModeSweep(1.0f); }

TEST_CASE("Kobber K35 Mode sweeps smoothly and changes the held sound", "[kobber][filter][filter-type][k35]") { checkModeSweep(2.0f); }

// Development audition, hidden from normal runs: with VEKT_KOBBER_DUMP set to a directory, renders a held A2 saw with a
// slow filter-envelope sweep at full Resonance through Ladder and SVF at Drive 0, +12 and +24 dB, as 32-bit float
// WAV files each normalised to -20 dBFS RMS: for comparing character, since the switching gain is not calibrated yet.
TEST_CASE("Kobber renders Ladder and SVF audition files", "[.][kobber-audition]")
{
	const auto* directory = std::getenv("VEKT_KOBBER_DUMP");
	REQUIRE(directory != nullptr);
	const juce::File folder(directory);
	REQUIRE(folder.createDirectory().wasOk());
	for (const auto type : { 0.0f, 1.0f })
		for (const auto drive : { 0.0f, 12.0f, 24.0f })
		{
			vekt::kobber::PluginProcessor processor;
			setParameter(processor, parameters::filterType, type);
			setParameter(processor, parameters::filterDrive, drive);
			setParameter(processor, parameters::filterResonance, 100.0f);
			setParameter(processor, parameters::filterCutoff, 150.0f);
			setParameter(processor, parameters::filterKeyTracking, 0.0f);
			setParameter(processor, parameters::filterEnvelopeAmount, 100.0f);
			setParameter(processor, parameters::filterAttack, 2.5f);
			setParameter(processor, parameters::filterSustain, 0.0f);
			setParameter(processor, parameters::filterDecay, 2.5f);
			setParameter(processor, parameters::ampSustain, 100.0f);
			setParameter(processor, parameters::masterOutput, -12.0f);
			processor.prepareToPlay(48'000.0, 512);
			auto render = hold(processor, 5 * 48'000 / 512, 45);
			const auto level = std::hypot(render.getRMSLevel(0, 0, render.getNumSamples()), render.getRMSLevel(1, 0, render.getNumSamples()))
				/ std::sqrt(2.0f);
			REQUIRE(level > 0.0f);
			render.applyGain(0.1f / level);
			const auto file = folder.getChildFile(juce::String(type < 0.5f ? "ladder" : "svf") + "-drive" + juce::String(drive, 0) + ".wav");
			file.deleteFile();
			std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
			juce::WavAudioFormat format;
			auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions {}
				.withSampleRate(48'000.0).withNumChannels(2).withBitsPerSample(32));
			REQUIRE(writer != nullptr);
			REQUIRE(writer->writeFromAudioSampleBuffer(render, 0, render.getNumSamples()));
		}
}

// Development measurement, hidden: output level with the Cutoff knob at its minimum, re fully open, for both filters.
TEST_CASE("Kobber closed-filter leakage", "[.][kobber-closed]")
{
	for (const auto defaults : { false, true })
		for (const auto resonance : { 10.0f, 100.0f })
			for (const auto note : { 33, 45, 57 })
			{
				std::cout << (defaults ? "tracking 50 %, envelope 50 %" : "tracking 0, envelope 0") << ", Resonance " << resonance
					<< " %, note " << note << ":";
				for (const auto type : { 0.0f, 1.0f })
				{
					const auto level = [&](float cutoff)
					{
						vekt::kobber::PluginProcessor processor;
						setParameter(processor, parameters::filterType, type);
						setParameter(processor, parameters::filterCutoff, cutoff);
						setParameter(processor, parameters::filterResonance, resonance);
						if (!defaults)
						{
							setParameter(processor, parameters::filterKeyTracking, 0.0f);
							setParameter(processor, parameters::filterEnvelopeAmount, 0.0f);
						}
						processor.prepareToPlay(48'000.0, 512);
						const auto render = hold(processor, 94, note);
						double sum {}, mean {};
						const auto start = 48'000, end = render.getNumSamples();
						for (int sample = start; sample < end; ++sample) mean += render.getSample(0, sample);
						mean /= end - start;
						for (int sample = start; sample < end; ++sample) sum += std::pow(render.getSample(0, sample) - mean, 2.0);
						return std::make_pair(10.0 * std::log10(sum / (end - start)), 20.0 * std::log10(std::abs(mean) + 1.0e-12));
					};
					const auto open = level(20'000.0f).first;
					const auto [closed, dc] = level(5.0f);
					std::cout << "  " << (type < 0.5f ? "Ladder" : "SVF") << " " << juce::String(closed - open, 1) << " dB (DC "
						<< juce::String(dc, 1) << " dBFS)";
				}
				std::cout << "\n";
			}
}

namespace
{
// A controlled single-saw voice for level measurements: no tracking, contour, drift or velocity, so the cutoff is
// exactly the setting and the fundamental exactly the note.
vekt::kobber::KobberVoiceSettings measurementVoice(vekt::kobber::FilterType type, float cutoff, float resonance, float drive,
	float mode = -1.0f)
{
	vekt::kobber::KobberVoiceSettings settings {};
	settings.rangeOctaves = { 0, 0, 0 }; // 8'
	settings.semitone = settings.fine = settings.octave = {};
	settings.level = { 0.7f, 0.0f, 0.0f };
	settings.morph = { 2.0f, 2.0f, 2.0f };
	settings.pulseWidth = { 50.0f, 50.0f, 50.0f };
	settings.cutoff = cutoff;
	settings.resonance = resonance;
	settings.drive = drive;
	settings.ampAttack = 0.005f;
	settings.ampDecay = 0.25f;
	settings.ampSustain = 1.0f;
	settings.ampRelease = 0.3f;
	settings.filterAttack = 0.005f;
	settings.filterDecay = 0.5f;
	settings.filterSustain = 1.0f;
	settings.filterRelease = 0.4f;
	settings.unison = 1;
	settings.filterType = type;
	settings.filterMode = mode;
	return settings;
}

struct VoiceLevel
{
	double rms {}, fundamental {}; // dB
};

VoiceLevel measureVoice(const vekt::kobber::KobberVoiceSettings& settings, int note)
{
	constexpr double sampleRate = 48'000.0;
	vekt::kobber::KobberVoice voice;
	voice.prepare(sampleRate, 0x4d6f6e6fu);
	voice.setPanPosition(0.0f);
	voice.start(1, note, 0.8f, settings, true, false, 1);
	const auto frequency = 440.0 * std::pow(2.0, (note - 69) / 12.0);
	const auto settle = static_cast<int>(sampleRate);
	const auto window = static_cast<int>(std::round(std::round(0.5 * frequency) * sampleRate / frequency));
	double energy {}, inPhase {}, quadrature {};
	for (int sample = 0; sample < settle + window; ++sample)
	{
		float left {}, right {};
		voice.render(left, right, settings, 0.0f);
		if (sample < settle) continue;
		const auto phase = 2.0 * std::numbers::pi * frequency * sample / sampleRate;
		energy += static_cast<double>(left) * left;
		inPhase += left * std::sin(phase);
		quadrature += left * std::cos(phase);
	}
	return { 10.0 * std::log10(energy / window), 20.0 * std::log10(2.0 * std::hypot(inPhase, quadrature) / window) };
}
}

// Development measurement, hidden: Ladder-to-SVF switching level (SVF minus Ladder, dB) in LP, broadband RMS and
// the fundamental, over Resonance x Drive for notes and cutoffs with the fundamental in the passband (ADR 0006).
TEST_CASE("Kobber Ladder to SVF switching level", "[.][kobber-switch-gain]")
{
	for (const auto drive : { 0.0f, 12.0f, 24.0f })
	{
		std::cout << "\nDrive +" << drive << " dB: SVF - Ladder, RMS / fundamental (dB), per cutoff and note\n";
		for (const auto resonance : { 0.0f, 0.25f, 0.5f, 0.75f, 0.9f, 1.0f })
		{
			std::cout << "Res " << juce::String(100.0f * resonance, 0) << " %:";
			for (const auto cutoff : { 300.0f, 1'200.0f, 5'000.0f })
				for (const auto note : { 36, 48 })
				{
					const auto ladder = measureVoice(measurementVoice(vekt::kobber::FilterType::ladder, cutoff, resonance, drive), note);
					const auto svf = measureVoice(measurementVoice(vekt::kobber::FilterType::svf, cutoff, resonance, drive), note);
					std::cout << "  " << juce::String(cutoff, 0) << "/" << note << " " << juce::String(svf.rms - ladder.rms, 1) << "/"
						<< juce::String(svf.fundamental - ladder.fundamental, 1);
				}
			std::cout << "\n";
		}
	}
}

TEST_CASE("Kobber Ladder and SVF switch at a sensible level at Drive 0", "[kobber][filter][filter-type][switch-gain]")
{
	// ADR 0006's switching-gain policy on the controlled saw: under 6 dB everywhere, and within 3 dB below full
	// Resonance (where the Ladder's own resonant peak adds level the fundamental does not show).
	for (const auto resonance : { 0.0f, 0.5f, 0.9f, 1.0f })
		for (const auto note : { 36, 48 })
		{
			const auto ladder = measureVoice(measurementVoice(vekt::kobber::FilterType::ladder, 1'200.0f, resonance, 0.0f), note);
			const auto svf = measureVoice(measurementVoice(vekt::kobber::FilterType::svf, 1'200.0f, resonance, 0.0f), note);
			INFO("Resonance " << resonance << ", note " << note << ": SVF - Ladder " << svf.rms - ladder.rms << " dB RMS");
			CHECK(std::abs(svf.rms - ladder.rms) < 6.0);
			if (resonance < 1.0f) CHECK(std::abs(svf.rms - ladder.rms) <= 3.0);
		}
}

// Development measurement, hidden: the switching level per Mode at Drive 0, since the SVF's Resonance trim was
// derived from LP. SVF minus Ladder, RMS dB, for notes 36 and 48 at cutoff 1.2 kHz, and for HP also with the notes
// well above a 40 Hz cutoff, so the HP passband rather than its stopband slope is compared.
TEST_CASE("Kobber Ladder to SVF switching level per Mode", "[.][kobber-switch-gain-mode]")
{
	struct Row { float mode; float cutoff; const char* name; };
	for (const auto& [mode, cutoff, name] : { Row { -1.0f, 1'200.0f, "LP 1.2k" }, Row { 0.0f, 1'200.0f, "Notch 1.2k" },
		Row { 1.0f, 1'200.0f, "HP 1.2k" }, Row { 1.0f, 40.0f, "HP 40" } })
	{
		std::cout << name << ":";
		for (const auto resonance : { 0.0f, 0.25f, 0.5f, 0.75f, 0.9f, 1.0f })
		{
			std::cout << "  Res " << juce::String(100.0f * resonance, 0) << "%";
			for (const auto note : { 36, 48 })
			{
				const auto ladder = measureVoice(measurementVoice(vekt::kobber::FilterType::ladder, cutoff, resonance, 0.0f, mode), note);
				const auto svf = measureVoice(measurementVoice(vekt::kobber::FilterType::svf, cutoff, resonance, 0.0f, mode), note);
				std::cout << " " << juce::String(svf.rms - ladder.rms, 1) << "/" << juce::String(svf.fundamental - ladder.fundamental, 1);
			}
		}
		std::cout << "\n";
	}
}

// Development measurement, hidden: K35 switching level against the Ladder and the SVF (K35 minus each, broadband RMS
// dB), in LP, over Resonance x Drive for notes 36 / 48 at cutoffs 300 Hz / 1.2 kHz / 5 kHz (ADR 0007). Printed per
// Drive as the mean and range over cutoffs and notes.
TEST_CASE("Kobber K35 switching level", "[.][kobber-switch-gain-k35]")
{
	for (const auto drive : { 0.0f, 12.0f, 24.0f })
	{
		std::cout << "\nDrive +" << drive << " dB | Resonance | K35 - Ladder mean [min, max] | K35 - SVF mean [min, max] (RMS dB)\n";
		for (const auto resonance : { 0.0f, 0.5f, 0.8f, 0.9f, 0.95f, 1.0f })
		{
			std::vector<double> ladderGap, svfGap;
			for (const auto cutoff : { 300.0f, 1'200.0f, 5'000.0f })
				for (const auto note : { 36, 48 })
				{
					const auto k35 = measureVoice(measurementVoice(vekt::kobber::FilterType::korg35, cutoff, resonance, drive), note);
					ladderGap.push_back(k35.rms - measureVoice(measurementVoice(vekt::kobber::FilterType::ladder, cutoff, resonance, drive), note).rms);
					svfGap.push_back(k35.rms - measureVoice(measurementVoice(vekt::kobber::FilterType::svf, cutoff, resonance, drive), note).rms);
				}
			const auto summary = [](const std::vector<double>& gaps)
			{
				double sum {};
				for (const auto gap : gaps) sum += gap;
				return juce::String(sum / static_cast<double>(gaps.size()), 1) + " [" + juce::String(*std::min_element(gaps.begin(), gaps.end()), 1) + ", "
					+ juce::String(*std::max_element(gaps.begin(), gaps.end()), 1) + "]";
			};
			std::cout << "+" << drive << " | " << juce::String(100.0f * resonance, 0) << " % | " << summary(ladderGap) << " | " << summary(svfGap) << "\n";
		}
	}
}

TEST_CASE("Kobber K35 switches at a sensible level at Drive 0", "[kobber][filter][filter-type][k35][switch-gain]")
{
	// ADR 0006's switching policy for K35 against the Ladder (under 6 dB everywhere, within 3 dB below full Resonance),
	// and within 1.5 dB of the SVF, whose Resonance trim it shares, below full Resonance (ADR 0007). At 100 % the two
	// differ by design (K35 self-oscillates, the SVF reaches Q 20; 2 October 2026), so within 3 dB there.
	for (const auto resonance : { 0.0f, 0.5f, 0.9f, 1.0f })
		for (const auto note : { 36, 48 })
		{
			const auto k35 = measureVoice(measurementVoice(vekt::kobber::FilterType::korg35, 1'200.0f, resonance, 0.0f), note);
			const auto ladder = measureVoice(measurementVoice(vekt::kobber::FilterType::ladder, 1'200.0f, resonance, 0.0f), note);
			const auto svf = measureVoice(measurementVoice(vekt::kobber::FilterType::svf, 1'200.0f, resonance, 0.0f), note);
			INFO("Resonance " << resonance << ", note " << note << ": K35 - Ladder " << k35.rms - ladder.rms << " dB, K35 - SVF " << k35.rms - svf.rms << " dB");
			CHECK(std::abs(k35.rms - ladder.rms) < 6.0);
			if (resonance < 1.0f) CHECK(std::abs(k35.rms - ladder.rms) <= 3.0);
			CHECK(std::abs(k35.rms - svf.rms) <= (resonance < 1.0f ? 1.5 : 3.0));
		}
}

TEST_CASE("Kobber K35 renders its own sound", "[kobber][filter][filter-type][k35]")
{
	const auto render = [](float type)
	{
		vekt::kobber::PluginProcessor processor;
		setParameter(processor, parameters::filterType, type);
		setParameter(processor, parameters::filterResonance, 80.0f);
		processor.prepareToPlay(48'000.0, 512);
		return hold(processor, 32, 45, [](int) {});
	};
	const auto k35 = render(2.0f), ladder = render(0.0f), svf = render(1.0f);
	const auto samples = k35.getNumSamples();
	for (int channel = 0; channel < 2; ++channel)
		for (int sample = 0; sample < samples; ++sample) REQUIRE(std::isfinite(k35.getSample(channel, sample)));
	REQUIRE(k35.getRMSLevel(0, 0, samples) > 0.01f);
	REQUIRE_FALSE(identical(k35, ladder));
	REQUIRE_FALSE(identical(k35, svf));
}

TEST_CASE("Kobber K35 switches under a held note without a click", "[kobber][filter][filter-type][k35]")
{
	// From Ladder or SVF to K35 and back: each switch declicks like any other Filter Type change.
	for (const auto [from, to] : { std::pair { 0.0f, 2.0f }, std::pair { 2.0f, 0.0f }, std::pair { 1.0f, 2.0f }, std::pair { 2.0f, 1.0f } })
	{
		INFO("filter type " << from << " -> " << to);
		vekt::kobber::PluginProcessor processor;
		setParameter(processor, parameters::filterType, from);
		setParameter(processor, parameters::filterCutoff, 1'500.0f);
		setParameter(processor, parameters::filterResonance, 30.0f);
		processor.prepareToPlay(48'000.0, 512);
		constexpr int switchBlock = 24;
		const auto output = hold(processor, 48, 45, [&processor, to](int block)
		{
			if (block == switchBlock) setParameter(processor, parameters::filterType, to);
		});
		constexpr auto switchSample = switchBlock * 512;
		const auto steady = std::max(largestStep(output, switchSample - 4'096, switchSample),
			largestStep(output, switchSample + 4'096, switchSample + 8'192));
		const auto atSwitch = largestStep(output, switchSample - 1, switchSample + 256);
		INFO("steady " << steady << ", at switch " << atSwitch);
		REQUIRE(steady > 0.0f);
		REQUIRE(atSwitch <= 1.25f * steady);
	}
}

TEST_CASE("Kobber K35 stays finite under hostile modulation through the processor", "[kobber][filter][filter-type][k35]")
{
	for (const auto quality : { 0.0f, 6.0f }) // Off and 16x FIR, the highest internal rate
		for (const auto multicore : { 0.0f, 1.0f })
		{
			INFO("quality " << quality << ", multicore " << multicore);
			vekt::kobber::PluginProcessor processor;
			setParameter(processor, parameters::trackingOversampling, quality);
			setParameter(processor, parameters::multicore, multicore);
			setParameter(processor, parameters::filterType, 2.0f);
			setParameter(processor, parameters::filterResonance, 100.0f);
			setParameter(processor, parameters::filterDrive, 24.0f);
			setParameter(processor, parameters::unison, 2.0f);
			setParameter(processor, parameters::noiseType, 2.0f);
			setParameter(processor, parameters::noiseLevel, 60.0f);
			setParameter(processor, parameters::lfos[0].rate, 7.0f);
			setParameter(processor, parameters::lfos[0].filter, 4.0f);
			setParameter(processor, parameters::lfos[0].drive, 24.0f);
			// Mode swept across low-pass, the half blend and high-pass.
			setParameter(processor, parameters::filterMode, 0.0f);
			setParameter(processor, parameters::lfos[0].filterMode, 100.0f);
			setParameter(processor, parameters::filterEnvelopeAmount, 100.0f);
			processor.prepareToPlay(48'000.0, 512);
			const auto output = playChord(processor, 48 * 512);
			float peak {};
			for (int channel = 0; channel < 2; ++channel)
				for (int sample = 0; sample < output.getNumSamples(); ++sample)
				{
					REQUIRE(std::isfinite(output.getSample(channel, sample)));
					peak = std::max(peak, std::abs(output.getSample(channel, sample)));
				}
			CHECK(peak > 1.0e-3f);
			CHECK(peak < 16.0f);
		}
}

namespace
{
// DC of a held render over [start, end): consecutive windows of whole periods (about 0.1 s each). ratio is the mean
// over all windows re the RMS (signed); spread is the standard deviation of the window means re the RMS, i.e. slow
// sub-audio movement rather than a constant offset.
struct DcReading
{
	double ratio {}, spread {}, rms {};
};

DcReading readDc(const std::vector<float>& y, double frequency, int start, int end, double sampleRate = 48'000.0)
{
	const auto periods = std::max(1.0, std::round(0.1 * frequency));
	const auto window = static_cast<int>(std::round(periods * sampleRate / frequency));
	std::vector<double> means;
	double energy {};
	int count {};
	for (int first = start; first + window <= end; first += window)
	{
		double sum {};
		for (int sample = first; sample < first + window; ++sample)
		{
			sum += y[static_cast<std::size_t>(sample)];
			energy += static_cast<double>(y[static_cast<std::size_t>(sample)]) * y[static_cast<std::size_t>(sample)];
			++count;
		}
		means.push_back(sum / window);
	}
	double mean {};
	for (const auto value : means) mean += value;
	mean /= static_cast<double>(means.size());
	double variance {};
	for (const auto value : means) variance += (value - mean) * (value - mean);
	const auto rms = std::sqrt(energy / count);
	return { mean / rms, std::sqrt(variance / static_cast<double>(means.size())) / rms, rms };
}

juce::String signedDb(double ratio)
{
	// Level in dB, then the polarity of the mean: -36.7n is a negative mean 36.7 dB below the RMS.
	return juce::String(20.0 * std::log10(std::abs(ratio) + 1.0e-300), 1) + (ratio < 0.0 ? "n" : "p");
}

double noteHz(int note) { return 440.0 * std::pow(2.0, (note - 69) / 12.0); }

// A single voice's left output, with an optional release at releaseAt.
std::vector<float> renderHeldVoice(const vekt::kobber::KobberVoiceSettings& settings, int note, int samples, int releaseAt = -1)
{
	vekt::kobber::KobberVoice voice;
	voice.prepare(48'000.0, 0x4d6f6e6fu);
	voice.setPanPosition(0.0f);
	voice.start(1, note, 0.8f, settings, true, false, 1);
	std::vector<float> output(static_cast<std::size_t>(samples));
	for (int sample = 0; sample < samples; ++sample)
	{
		if (sample == releaseAt) voice.release(false);
		float left {}, right {};
		voice.render(left, right, settings, 0.0f);
		output[static_cast<std::size_t>(sample)] = left;
	}
	return output;
}

// The voice's filter output before its DC blocker (ADR 0008), reconstructed within numerical precision from a render
// with known amplitude: divide by the amplitude, then apply the first-order blocker's inverse,
// x[n] = y[n] - a y[n-1] + x[n-1]. This is well conditioned: the float rounding of y enters the running sum only
// through 1 - a (6.5e-4 at 48 kHz).
std::vector<double> unblockedFilterOutput(const std::vector<float>& output, const std::vector<float>& amplitude)
{
	const auto a = std::exp(-2.0 * std::numbers::pi * vekt::kobber::filterOutputDcBlockerHz / 48'000.0);
	std::vector<double> unblocked(output.size());
	double previousBlocked {}, previousUnblocked {};
	for (std::size_t sample = 0; sample < output.size(); ++sample)
	{
		const auto blocked = amplitude[sample] > 0.0f ? static_cast<double>(output[sample]) / amplitude[sample] : 0.0;
		unblocked[sample] = blocked - a * previousBlocked + previousUnblocked;
		previousBlocked = blocked;
		previousUnblocked = unblocked[sample];
	}
	return unblocked;
}

// A band-limited saw at the given note, peak `level`, from the voice's own oscillator.
std::vector<float> bandLimitedSaw(int note, int samples, float level, float pulseWidth = 50.0f, float morph = 2.0f)
{
	const auto frequency = 440.0 * std::pow(2.0, (note - 69) / 12.0);
	std::vector<float> saw(static_cast<std::size_t>(samples));
	vekt::kobber::WidthOscillatorState state;
	double phase {};
	for (auto& value : saw)
	{
		phase += frequency / 48'000.0;
		phase -= std::floor(phase);
		value = level * vekt::kobber::renderWidthOscillator(state, static_cast<float>(phase), static_cast<float>(frequency), 48'000.0, morph, pulseWidth, false);
	}
	return saw;
}

// A bare filter (Ladder, SVF low-pass or K35, from rest, untrimmed) at 48 kHz: what the voice's DC blocker receives.
std::vector<float> renderBareFilter(vekt::kobber::FilterType type, float cutoff, float resonance, float drive, const std::vector<float>& input)
{
	std::vector<float> y(input.size());
	vekt::kobber::NonlinearTptLadder ladder;
	vekt::kobber::NonlinearTptSvf svf;
	vekt::kobber::NonlinearTptKorg35 korg;
	ladder.prepare(48'000.0);
	svf.prepare(48'000.0);
	korg.prepare(48'000.0);
	const vekt::kobber::NonlinearTptLadderSettings ladderSettings { cutoff, resonance, drive, false, 0.0f, -1.0f };
	const vekt::kobber::NonlinearTptSvfSettings svfSettings { cutoff, vekt::kobber::svfDamping(resonance), drive, vekt::kobber::svfKnee,
		vekt::kobber::svfDampingCurve };
	vekt::kobber::NonlinearTptKorg35Settings korgSettings;
	korgSettings.cutoffHz = cutoff;
	korgSettings.feedback = vekt::kobber::korg35Feedback(resonance);
	korgSettings.driveDecibels = drive;
	korgSettings.knee = vekt::kobber::korg35Knee;
	for (std::size_t n = 0; n < y.size(); ++n)
		y[n] = type == vekt::kobber::FilterType::ladder ? ladder.processCoupled(input[n], ladderSettings)
			: type == vekt::kobber::FilterType::svf ? static_cast<float>(svf.process(input[n], svfSettings).lowPass)
			: static_cast<float>(korg.process(input[n], korgSettings));
	return y;
}

// The voice's amplitude (amp envelope x allocation fade) as KobberVoice computes it, for measurementVoice settings.
std::vector<float> voiceAmplitude(const vekt::kobber::KobberVoiceSettings& settings, int samples, int releaseAt = -1)
{
	vekt::kobber::ContourEnvelope amp;
	amp.setSampleRate(48'000.0);
	amp.setParameters({ settings.ampAttack, settings.ampDecay, settings.ampSustain, settings.ampRelease });
	amp.noteOn();
	const auto transition = static_cast<int>(std::round(0.003 * 48'000.0));
	std::vector<float> amplitude(static_cast<std::size_t>(samples));
	for (int sample = 0; sample < samples; ++sample)
	{
		if (sample == releaseAt) amp.noteOff();
		const auto fade = sample < transition ? 1.0f - static_cast<float>(transition - sample) / static_cast<float>(transition) : 1.0f;
		amplitude[static_cast<std::size_t>(sample)] = amp.getNextSample() * 1.0f * fade;
	}
	return amplitude;
}
}

// Development measurement, hidden: DC at the output of the Ladder and SVF. A controlled single saw voice (no tracking,
// contour or drift, amp sustain 100 %, so the output is the filter output times a constant), held 1.5 s, measured over
// 0.5-1.5 s: mean re RMS (spread of 0.1 s window means re RMS) in dB, of the filter output before the voice's DC
// blocker (reconstructed by unblockedFilterOutput), then after it, i.e. the voice output (ADR 0008).
TEST_CASE("Kobber Ladder and SVF output DC", "[.][kobber-dc]")
{
	constexpr int samples = 72'000;
	for (const auto type : { vekt::kobber::FilterType::ladder, vekt::kobber::FilterType::svf })
		for (const auto drive : { 0.0f, 12.0f, 24.0f })
		{
			std::cout << "\n" << (type == vekt::kobber::FilterType::ladder ? "Ladder" : "SVF") << " Drive +" << drive
				<< " dB | Res | cutoff | note 33 / 45 / 57: DC re RMS (spread) dB\n";
			for (const auto resonance : { 0.0f, 0.5f, 0.9f })
				for (const auto cutoff : { 300.0f, 2'000.0f, 5'000.0f })
				{
					std::cout << juce::String(100.0f * resonance, 0) << " % | " << cutoff << " |";
					for (const auto note : { 33, 45, 57 })
					{
						const auto settings = measurementVoice(type, cutoff, resonance, drive);
						const auto y = renderHeldVoice(settings, note, samples);
						const auto unblocked = unblockedFilterOutput(y, voiceAmplitude(settings, samples));
						const auto dc = readDc(std::vector<float>(unblocked.begin(), unblocked.end()), noteHz(note), 24'000, samples);
						std::cout << " " << signedDb(dc.ratio) << " (" << juce::String(20.0 * std::log10(dc.spread + 1.0e-300), 0) << ") -> "
							<< signedDb(readDc(y, noteHz(note), 24'000, samples).ratio);
					}
					std::cout << "\n";
				}
		}
}

// Development measurement, hidden: where the DC comes from. (a) The oscillator alone: mean re RMS of each Morph anchor
// and a 25 % pulse, raw and zero-centred. (b) Through each filter at Res 90 %, 2 kHz, note 45 (the voice's filter output
// before its DC blocker): the same waveforms, and the saw per Mode.
TEST_CASE("Kobber Ladder and SVF output DC sources", "[.][kobber-dc-source]")
{
	struct Shape { float morph, width; const char* name; };
	const std::array shapes { Shape { 0.0f, 50.0f, "sine" }, Shape { 1.0f, 50.0f, "triangle" }, Shape { 2.0f, 50.0f, "saw" },
		Shape { 3.0f, 50.0f, "square" }, Shape { 3.0f, 25.0f, "pulse 25 %" }, Shape { 2.0f, 25.0f, "saw W 25 %" } };
	std::cout << "\nOscillator alone, note 45 | mean re RMS dB, raw / zero-centred\n";
	for (const auto& shape : shapes)
	{
		std::cout << shape.name << " |";
		for (const auto zeroCentred : { false, true })
		{
			vekt::kobber::WidthOscillatorState state;
			std::vector<float> y(48'000);
			double phase {};
			const auto frequency = noteHz(45);
			for (auto& value : y)
			{
				phase += frequency / 48'000.0;
				phase -= std::floor(phase);
				value = vekt::kobber::renderWidthOscillator(state, static_cast<float>(phase), static_cast<float>(frequency), 48'000.0, shape.morph, shape.width, zeroCentred);
			}
			std::cout << " " << signedDb(readDc(y, frequency, 4'800, 48'000).ratio);
		}
		std::cout << "\n";
	}
	for (const auto type : { vekt::kobber::FilterType::ladder, vekt::kobber::FilterType::svf })
	{
		std::cout << "\n" << (type == vekt::kobber::FilterType::ladder ? "Ladder" : "SVF")
			<< ", Res 90 %, 2 kHz, note 45 | Drive 0 / +12 / +24: DC re RMS dB (raw policy; zero-centred)\n";
		for (const auto& shape : shapes)
		{
			std::cout << shape.name << " |";
			for (const auto drive : { 0.0f, 12.0f, 24.0f })
			{
				auto settings = measurementVoice(type, 2'000.0f, 0.9f, drive);
				settings.morph[0] = shape.morph;
				settings.pulseWidth[0] = shape.width;
				const auto beforeBlocker = [&settings]
				{
					const auto unblocked = unblockedFilterOutput(renderHeldVoice(settings, 45, 72'000), voiceAmplitude(settings, 72'000));
					return readDc(std::vector<float>(unblocked.begin(), unblocked.end()), noteHz(45), 24'000, 72'000);
				};
				const auto raw = beforeBlocker();
				settings.widthDcPolicy = vekt::kobber::WidthDcPolicy::zeroCentered;
				const auto centred = beforeBlocker();
				std::cout << " " << signedDb(raw.ratio) << "; " << signedDb(centred.ratio);
			}
			std::cout << "\n";
		}
		for (const auto mode : { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f })
		{
			std::cout << "saw, Mode " << mode << " |";
			for (const auto drive : { 0.0f, 12.0f, 24.0f })
			{
				const auto settings = measurementVoice(type, 2'000.0f, 0.9f, drive, mode);
				const auto unblocked = unblockedFilterOutput(renderHeldVoice(settings, 45, 72'000), voiceAmplitude(settings, 72'000));
				std::cout << " " << signedDb(readDc(std::vector<float>(unblocked.begin(), unblocked.end()), noteHz(45), 24'000, 72'000).ratio);
			}
			std::cout << "\n";
		}
	}
}

// Development measurement, hidden: the ADR 0007 case through the whole PluginProcessor (48 kHz, 1x; Res 90 %, 2 kHz,
// filter envelope 0, amp sustain 100 %, Drift 0), over 1-2 s of the left channel: DC re RMS dB (spread of 0.1 s
// window means re RMS). Two patches: the processor's start-up sound as the ADR measured it (the first factory preset:
// unison 2x, three detuned oscillators, delayed vibrato), and a plain saw (Osc 1 only, Morph 2, no fine, unison 1,
// no LFO). One note (45) and a four-note chord (45 / 52 / 57 / 61). Windows cover whole periods of note 45 only, so
// detuned layers, vibrato and the chord show up as spread. Since ADR 0008 this is the output after the voices' DC
// blockers; the numbers before it are in ADR 0008.
TEST_CASE("Kobber Ladder and SVF output DC through the processor", "[.][kobber-dc-processor]")
{
	for (const auto plain : { false, true })
		for (const auto type : { 0.0f, 1.0f })
		{
			std::cout << "\n" << (plain ? "plain saw, " : "start-up patch, ") << (type == 0.0f ? "Ladder" : "SVF")
				<< " | Drive 0 / +12 / +24: one note; chord\n";
			for (const auto drive : { 0.0f, 12.0f, 24.0f })
			{
				for (const auto chord : { false, true })
				{
					vekt::kobber::PluginProcessor processor;
					setParameter(processor, parameters::filterType, type);
					setParameter(processor, parameters::filterResonance, 90.0f);
					setParameter(processor, parameters::filterCutoff, 2'000.0f);
					setParameter(processor, parameters::filterDrive, drive);
					setParameter(processor, parameters::filterEnvelopeAmount, 0.0f);
					setParameter(processor, parameters::ampSustain, 100.0f);
					setParameter(processor, parameters::drift, 0.0f);
					if (plain)
					{
						setParameter(processor, parameters::unison, 0.0f);
						setParameter(processor, parameters::osc1Fine, 0.0f);
						setParameter(processor, parameters::osc1Morph, 2.0f);
						setParameter(processor, parameters::osc2Level, 0.0f);
						setParameter(processor, parameters::osc3Level, 0.0f);
						setParameter(processor, parameters::vibratoDepth, 0.0f);
						for (const auto* pitch : parameters::lfos[0].pitch)
							setParameter(processor, pitch, 0.0f);
					}
					processor.prepareToPlay(48'000.0, 512);
					const auto output = hold(processor, 188, chord ? -1 : 45, [&](int block)
					{
						if (!chord || block != 0) return;
						juce::MidiBuffer midi;
						for (const auto note : { 45, 52, 57, 61 }) midi.addEvent(juce::MidiMessage::noteOn(1, note, 0.8f), 0);
						juce::AudioBuffer<float> first(2, 512);
						processor.processBlock(first, midi); // the chord starts one block early; the analysis is later
					});
					std::vector<float> left(output.getReadPointer(0), output.getReadPointer(0) + output.getNumSamples());
					const auto dc = readDc(left, noteHz(45), 48'000, output.getNumSamples());
					std::cout << " " << signedDb(dc.ratio) << " (" << juce::String(20.0 * std::log10(dc.spread + 1.0e-300), 0) << ")";
				}
				std::cout << " |";
			}
			std::cout << "\n";
		}
}

// Development measurement, hidden: is the DC audible at note on / off? The voice's filter output s before its DC blocker
// (a held render with a 0.5 ms attack through unblockedFilterOutput; the replica error of the blocked version against
// a real render is printed) has a steady
// mean m. Unblocked, the DC reaches the output as the pedestal m x amplitude, switched by the amp envelope. With a
// 5 Hz DcBlocker between filter and amp (reset at note start), what is left of it is the blocker's response to the
// DC step m (it is linear), times the amplitude: m exp(-t / 32 ms) x amplitude at note on, nothing by note off.
// Per 50 ms window at note on and note off, in the thump band (20 Hz to half the fundamental: two one-pole high-passes
// at 20 Hz, two one-pole low-passes at f0 / 2): the pedestal's energy re the note's own energy in that band, and re
// the note's whole energy in the window; then the same for what the blocker leaves.
TEST_CASE("Kobber Ladder and SVF output DC at note on and off", "[.][kobber-dc-thump]")
{
	constexpr int samples = 72'000, releaseAt = 48'000, window = 2'400;
	struct Envelope { float attack, release; const char* name; };
	for (const auto type : { vekt::kobber::FilterType::ladder, vekt::kobber::FilterType::svf })
		for (const auto drive : { 0.0f, 24.0f })
		{
			std::cout << "\n" << (type == vekt::kobber::FilterType::ladder ? "Ladder" : "SVF") << ", Drive +" << drive
				<< ", Res 90 %, 2 kHz | envelope | note | DC re RMS | on: pedestal re band, re all; blocked re band, re all"
				<< " | off: same (dB) | replica error\n";
			for (const auto& envelope : { Envelope { 0.005f, 0.3f, "5 ms / 300 ms" }, Envelope { 0.0005f, 0.005f, "0.5 ms / 5 ms" } })
				for (const auto note : { 33, 45, 69, 81 })
				{
					auto fast = measurementVoice(type, 2'000.0f, 0.9f, drive);
					fast.ampAttack = 0.0005f;
					const auto preAmp = unblockedFilterOutput(renderHeldVoice(fast, note, samples), voiceAmplitude(fast, samples));
					std::vector<float> steady(preAmp.begin() + 24'000, preAmp.begin() + 72'000);
					const auto reading = readDc(steady, noteHz(note), 0, 48'000);
					const auto mean = reading.ratio * reading.rms;
					auto settings = fast;
					settings.ampAttack = envelope.attack;
					settings.ampRelease = envelope.release;
					const auto amplitude = voiceAmplitude(settings, samples, releaseAt);
					const auto real = renderHeldVoice(settings, note, samples, releaseAt);
					vekt::dsp::DcBlocker<double> blocker, voiceBlocker;
					blocker.prepare(48'000.0, vekt::kobber::filterOutputDcBlockerHz);
					voiceBlocker.prepare(48'000.0, vekt::kobber::filterOutputDcBlockerHz);
					std::vector<double> plain(preAmp.size()), pedestal(preAmp.size()), residual(preAmp.size());
					double replicaError {};
					for (std::size_t sample = 0; sample < preAmp.size(); ++sample)
					{
						plain[sample] = preAmp[sample] * amplitude[sample];
						pedestal[sample] = mean * amplitude[sample];
						residual[sample] = blocker.processSample(sample == 0 ? 0.0 : mean) * amplitude[sample];
						replicaError = std::max(replicaError, std::abs(voiceBlocker.processSample(preAmp[sample]) * amplitude[sample] - real[sample]));
					}
					const auto band = [note](const std::vector<double>& x)
					{
						const auto pole = [](double hz) { return std::exp(-2.0 * std::numbers::pi * hz / 48'000.0); };
						const auto high = pole(20.0), low = pole(0.5 * noteHz(note));
						std::vector<double> y(x.size());
						double h1x {}, h1y {}, h2x {}, h2y {}, l1 {}, l2 {};
						for (std::size_t n = 0; n < x.size(); ++n)
						{
							h1y = x[n] - h1x + high * h1y; h1x = x[n];
							h2y = h1y - h2x + high * h2y; h2x = h1y;
							l1 = h2y + low * (l1 - h2y);
							l2 = l1 + low * (l2 - l1);
							y[n] = l2;
						}
						return y;
					};
					// The note without its DC: what the pedestal is heard against.
					std::vector<double> note0(plain.size());
					for (std::size_t sample = 0; sample < plain.size(); ++sample) note0[sample] = plain[sample] - pedestal[sample];
					const auto noteBand = band(note0), pedestalBand = band(pedestal), residualBand = band(residual);
					const auto energy = [](const std::vector<double>& x, int start)
					{
						double sum {};
						for (int sample = start; sample < start + window; ++sample) sum += x[static_cast<std::size_t>(sample)] * x[static_cast<std::size_t>(sample)];
						return sum;
					};
					const auto db = [](double ratio) { return juce::String(10.0 * std::log10(ratio + 1.0e-300), 0); };
					const auto report = [&](int start)
					{
						const auto noteInBand = energy(noteBand, start), noteAll = energy(note0, start);
						return db(energy(pedestalBand, start) / noteInBand) + ", " + db(energy(pedestalBand, start) / noteAll) + "; "
							+ db(energy(residualBand, start) / noteInBand) + ", " + db(energy(residualBand, start) / noteAll);
					};
					std::cout << envelope.name << " | " << note << " | " << signedDb(reading.ratio) << " | " << report(0) << " | "
						<< report(releaseAt) << " | " << juce::String(replicaError, 9) << "\n";
				}
		}
}

// Development measurement, hidden: is the Ladder / SVF / K35 DC a property of the saturation, or a numerical bias?
// Each bare filter (K35 before its output blocker) from rest, fed a band-limited saw x (note 45, 0.7) and then -x:
// an odd-symmetric implementation gives DC(-x) = -DC(x) and y(-x) = -y(x). Then the level: at input x 0.1 and x 0.01
// the DC of a saturation product should fall about 40 dB per decade (the cubic term), a bias would not.
TEST_CASE("Kobber Ladder, SVF and K35 DC polarity and level", "[.][kobber-dc-polarity]")
{
	constexpr int samples = 72'000;
	const auto frequency = noteHz(45);
	const auto saw = bandLimitedSaw(45, samples, 0.7f);
	const auto run = [&](int type, float resonance, float drive, float gain)
	{
		auto input = saw;
		for (auto& value : input) value *= gain;
		return renderBareFilter(static_cast<vekt::kobber::FilterType>(type), 2'000.0f, resonance, drive, input);
	};
	for (const auto type : { 0, 1, 2 })
	{
		std::cout << "\n" << (type == 0 ? "Ladder" : type == 1 ? "SVF" : "K35 (raw)")
			<< ", 2 kHz | Res | Drive | DC(x) / DC(-x) re RMS dB | max |y(x) + y(-x)| re peak dB | DC at input x 0.1 / x 0.01 (Drive 0)\n";
		for (const auto resonance : { 0.5f, 0.9f })
			for (const auto drive : { 0.0f, 12.0f, 24.0f })
			{
				const auto positive = run(type, resonance, drive, 1.0f), negative = run(type, resonance, drive, -1.0f);
				double mismatch {}, peak {};
				for (std::size_t n = 0; n < positive.size(); ++n)
				{
					mismatch = std::max(mismatch, static_cast<double>(std::abs(positive[n] + negative[n])));
					peak = std::max(peak, static_cast<double>(std::abs(positive[n])));
				}
				std::cout << juce::String(100.0f * resonance, 0) << " % | +" << drive << " | "
					<< signedDb(readDc(positive, frequency, 24'000, samples).ratio) << " / " << signedDb(readDc(negative, frequency, 24'000, samples).ratio)
					<< " | " << juce::String(20.0 * std::log10(mismatch / peak + 1.0e-300), 1);
				if (drive == 0.0f)
					std::cout << " | " << signedDb(readDc(run(type, resonance, 0.0f, 0.1f), frequency, 24'000, samples).ratio) << " / "
						<< signedDb(readDc(run(type, resonance, 0.0f, 0.01f), frequency, 24'000, samples).ratio);
				std::cout << "\n";
			}
	}
}

// Development measurement, hidden: short notes against a 5 Hz blocker. Ladder, Res 90 %, 2 kHz, +24 dB (the worst
// held case), 5 ms attack and 5 ms release, notes of 20 / 50 / 100 / 500 ms. In the 50 ms from note off, in the
// thump band: the DC pedestal unblocked, and what a blocker at 5 Hz (and, for comparison, 10 and 20 Hz) leaves of the
// DC step, re the note's own energy in that band / re the note's whole energy (dB). The pedestal uses the settled mean;
// the filter's own DC settles within a few ms at 2 kHz.
TEST_CASE("Kobber Ladder output DC on short notes", "[.][kobber-dc-short]")
{
	constexpr int samples = 48'000, window = 2'400;
	std::cout << "\nLadder +24 dB, Res 90 %, 2 kHz, 5 ms / 5 ms | note | length | unblocked | 5 Hz | 10 Hz | 20 Hz\n";
	for (const auto note : { 45, 69, 81 })
	{
		auto settings = measurementVoice(vekt::kobber::FilterType::ladder, 2'000.0f, 0.9f, 24.0f);
		settings.ampAttack = 0.0005f;
		const auto preAmp = unblockedFilterOutput(renderHeldVoice(settings, note, samples), voiceAmplitude(settings, samples));
		std::vector<float> steady(preAmp.begin() + 24'000, preAmp.end());
		const auto reading = readDc(steady, noteHz(note), 0, 24'000);
		const auto mean = reading.ratio * reading.rms;
		for (const auto lengthMs : { 20, 50, 100, 500 })
		{
			const auto releaseAt = lengthMs * 48;
			settings.ampAttack = 0.005f;
			settings.ampRelease = 0.005f;
			const auto amplitude = voiceAmplitude(settings, samples, releaseAt);
			const auto band = [note](const std::vector<double>& x)
			{
				const auto pole = [](double hz) { return std::exp(-2.0 * std::numbers::pi * hz / 48'000.0); };
				const auto high = pole(20.0), low = pole(0.5 * noteHz(note));
				std::vector<double> y(x.size());
				double h1x {}, h1y {}, h2x {}, h2y {}, l1 {}, l2 {};
				for (std::size_t n = 0; n < x.size(); ++n)
				{
					h1y = x[n] - h1x + high * h1y; h1x = x[n];
					h2y = h1y - h2x + high * h2y; h2x = h1y;
					l1 = h2y + low * (l1 - h2y);
					l2 = l1 + low * (l2 - l1);
					y[n] = l2;
				}
				return y;
			};
			std::vector<double> note0(preAmp.size()), pedestal(preAmp.size());
			for (std::size_t n = 0; n < preAmp.size(); ++n)
			{
				pedestal[n] = mean * amplitude[n];
				note0[n] = (preAmp[n] - mean) * amplitude[n];
			}
			const auto energy = [&](const std::vector<double>& x)
			{
				double sum {};
				for (int n = releaseAt; n < releaseAt + window; ++n) sum += x[static_cast<std::size_t>(n)] * x[static_cast<std::size_t>(n)];
				return sum;
			};
			const auto noteBand = energy(band(note0)), noteAll = energy(note0);
			const auto describe = [&](const std::vector<double>& x)
			{
				const auto inBand = energy(band(x));
				return juce::String(10.0 * std::log10(inBand / noteBand + 1.0e-300), 0) + " / " + juce::String(10.0 * std::log10(inBand / noteAll + 1.0e-300), 0);
			};
			std::cout << note << " | " << lengthMs << " ms | " << describe(pedestal);
			for (const auto cutoff : { 5.0, 10.0, 20.0 })
			{
				vekt::dsp::DcBlocker<double> blocker;
				blocker.prepare(48'000.0, cutoff);
				std::vector<double> left(preAmp.size());
				for (std::size_t n = 0; n < preAmp.size(); ++n) left[n] = blocker.processSample(n == 0 ? 0.0 : mean) * amplitude[n];
				std::cout << " | " << describe(left);
			}
			std::cout << "\n";
		}
	}
}

TEST_CASE("Kobber filter-output DC blocker keeps the low end", "[kobber][filter][filter-type][dc]")
{
	// First order at 5 Hz (ADR 0008): about -3 dB there and within 0.3 dB from 20 Hz up, at the base and at the 16x rate.
	for (const auto sampleRate : { 48'000.0, vekt::dsp::maximumInternalSampleRate })
	{
		const auto gainAt = [sampleRate](double frequency)
		{
			vekt::dsp::DcBlocker<double> blocker;
			blocker.prepare(sampleRate, vekt::kobber::filterOutputDcBlockerHz);
			const auto period = sampleRate / frequency;
			double peak {};
			for (long sample = 0; sample < static_cast<long>(10.0 * sampleRate); ++sample)
			{
				const auto y = blocker.processSample(std::sin(2.0 * std::numbers::pi * static_cast<double>(sample) / period));
				if (sample > static_cast<long>(8.0 * sampleRate)) peak = std::max(peak, std::abs(y));
			}
			return 20.0 * std::log10(peak);
		};
		INFO("rate " << sampleRate);
		CHECK(gainAt(20.0) > -0.3);
		CHECK(std::abs(gainAt(5.0) + 3.0) < 0.2);
	}
}

TEST_CASE("Kobber removes every filter's DC at the filter output", "[kobber][filter][filter-type][dc]")
{
	// A driven saw into each filter's stress case: the bare filter generates tens of dB of DC (its saturation on a
	// waveform without half-wave symmetry), and the held voice's settled output carries none of it.
	struct Case { vekt::kobber::FilterType type; float cutoff, resonance; double bareAbove; };
	for (const auto& [type, cutoff, resonance, bareAbove] : { Case { vekt::kobber::FilterType::ladder, 300.0f, 0.9f, -15.0 },
		Case { vekt::kobber::FilterType::svf, 300.0f, 0.5f, -35.0 }, Case { vekt::kobber::FilterType::korg35, 2'000.0f, 0.9f, -40.0 } })
	{
		constexpr int samples = 72'000;
		const auto bare = readDc(renderBareFilter(type, cutoff, resonance, 24.0f, bandLimitedSaw(45, samples, 0.7f)), noteHz(45), 24'000, samples);
		const auto voice = readDc(renderHeldVoice(measurementVoice(type, cutoff, resonance, 24.0f), 45, samples), noteHz(45), 48'000, samples);
		const auto db = [](double ratio) { return 20.0 * std::log10(std::abs(ratio) + 1.0e-300); };
		INFO("type " << static_cast<int>(type) << ": bare filter DC " << db(bare.ratio) << " dB re RMS, voice " << db(voice.ratio) << " dB");
		CHECK(db(bare.ratio) > bareAbove);
		CHECK(db(voice.ratio) < -80.0);
	}
}

TEST_CASE("Kobber raw pulse keeps its DC into the filter and none at the voice output", "[kobber][filter][filter-type][dc][width]")
{
	// WidthDcPolicy::raw is deliberate: a narrow pulse keeps its mean, which biases the filter's saturation. The voice
	// removes DC only after the filter, so the filter's nonlinear response to the bias stays materially different (the
	// settled raw and zero-centred outputs differ) while neither output carries DC.
	constexpr int samples = 72'000;
	const auto db = [](double ratio) { return 20.0 * std::log10(std::abs(ratio) + 1.0e-300); };
	const auto pulse = bandLimitedSaw(45, samples, 1.0f, 25.0f, 3.0f);
	CHECK(db(readDc(pulse, noteHz(45), 4'800, samples).ratio) > -8.0);
	auto input = pulse;
	for (auto& value : input) value *= 0.7f;
	CHECK(db(readDc(renderBareFilter(vekt::kobber::FilterType::ladder, 1'000.0f, 0.5f, 12.0f, input), noteHz(45), 24'000, samples).ratio) > -15.0);
	auto settings = measurementVoice(vekt::kobber::FilterType::ladder, 1'000.0f, 0.5f, 12.0f);
	settings.morph[0] = 3.0f;
	settings.pulseWidth[0] = 25.0f;
	const auto raw = renderHeldVoice(settings, 45, samples);
	settings.widthDcPolicy = vekt::kobber::WidthDcPolicy::zeroCentered;
	const auto centred = renderHeldVoice(settings, 45, samples);
	const auto rawDc = readDc(raw, noteHz(45), 48'000, samples), centredDc = readDc(centred, noteHz(45), 48'000, samples);
	double difference {};
	for (int sample = 48'000; sample < samples; ++sample)
		difference += std::pow(static_cast<double>(raw[static_cast<std::size_t>(sample)] - centred[static_cast<std::size_t>(sample)]), 2.0);
	const auto differenceDb = 10.0 * std::log10(difference / (samples - 48'000)) - 20.0 * std::log10(rawDc.rms);
	INFO("voice DC raw " << db(rawDc.ratio) << " dB, zero-centred " << db(centredDc.ratio) << " dB; raw - centred " << differenceDb << " dB re RMS");
	CHECK(db(rawDc.ratio) < -80.0);
	CHECK(db(centredDc.ratio) < -80.0);
	CHECK(differenceDb > -30.0);
}

TEST_CASE("Kobber filter-output DC blocker has mostly settled by the end of a 100 ms note", "[kobber][filter][filter-type][dc]")
{
	// The blocker's time constant is 32 ms, so the DC step at note on is only partly removed on very short notes (20 ms
	// notes keep most of it: a known limit, characterised by [kobber-dc-short]). By 100 ms, when a fast release would expose
	// it, it is at least 20 dB down. Worst held case: Ladder, +24 dB, Res 90 %, 2 kHz, note 69, 0.5 ms attack.
	constexpr int samples = 48'000;
	auto settings = measurementVoice(vekt::kobber::FilterType::ladder, 2'000.0f, 0.9f, 24.0f);
	settings.ampAttack = 0.0005f;
	const auto output = renderHeldVoice(settings, 69, samples);
	const auto amplitude = voiceAmplitude(settings, samples);
	const auto unblocked = unblockedFilterOutput(output, amplitude);
	const auto settled = readDc(std::vector<float>(unblocked.begin(), unblocked.end()), noteHz(69), 24'000, samples);
	const auto dc = settled.ratio * settled.rms;
	// The blocked filter output over the nine whole periods before 100 ms.
	const auto window = static_cast<int>(std::round(9.0 * 48'000.0 / noteHz(69)));
	double sum {};
	for (int sample = 4'800 - window; sample < 4'800; ++sample)
		sum += static_cast<double>(output[static_cast<std::size_t>(sample)]) / amplitude[static_cast<std::size_t>(sample)];
	const auto remainingDb = 20.0 * std::log10(std::abs(sum / window / dc));
	INFO("DC " << 20.0 * std::log10(std::abs(settled.ratio)) << " dB re RMS unblocked; at 80-100 ms " << remainingDb << " dB of it remains");
	CHECK(20.0 * std::log10(std::abs(settled.ratio)) > -15.0);
	CHECK(remainingDb < -20.0);
}

namespace
{
// One voice reused after a natural note end, as the processor does: `first` held for holdSamples, released and rendered
// until the voice ends (its filters and DC blockers keep their state, as in the processor), gapSamples of the idle voice,
// then `second` on the same voice for secondSamples. firstOutput holds note 1's rendered samples up to its end.
struct ReusedVoiceRender
{
	std::vector<float> firstOutput, second;
};

ReusedVoiceRender renderReusedVoice(const vekt::kobber::KobberVoiceSettings& first, int firstNote, int holdSamples, int gapSamples,
	const vekt::kobber::KobberVoiceSettings& second, int secondNote, int secondSamples)
{
	vekt::kobber::KobberVoice voice;
	voice.prepare(48'000.0, 0x4d6f6e6fu);
	voice.setPanPosition(0.0f);
	voice.start(1, firstNote, 0.8f, first, true, false, 1);
	ReusedVoiceRender render;
	for (int sample = 0; voice.isActive() && sample < holdSamples + 480'000; ++sample)
	{
		if (sample == holdSamples) voice.release(false);
		float left {}, right {};
		voice.render(left, right, first, 0.0f);
		render.firstOutput.push_back(left);
	}
	REQUIRE_FALSE(voice.isActive());
	for (int sample = 0; sample < gapSamples; ++sample)
	{
		float left {}, right {};
		voice.render(left, right, second, 0.0f);
		REQUIRE(left == 0.0f);
	}
	voice.start(1, secondNote, 0.8f, second, true, false, 2);
	for (int sample = 0; sample < secondSamples; ++sample)
	{
		float left {}, right {};
		voice.render(left, right, second, 0.0f);
		render.second.push_back(left);
	}
	return render;
}

// The mean of a pre-amp signal over the whole periods in its first 50 ms, re `rms`: the DC step a note starts with.
double onsetDcRatio(const std::vector<double>& preAmp, double frequency, double rms)
{
	const auto periods = std::max(1.0, std::round(0.05 * frequency));
	const auto window = static_cast<int>(std::round(periods * 48'000.0 / frequency));
	double sum {};
	for (int sample = 1; sample <= window; ++sample) sum += preAmp[static_cast<std::size_t>(sample)];
	return sum / window / rms;
}

std::vector<double> preAmpOf(const std::vector<float>& output, const std::vector<float>& amplitude)
{
	std::vector<double> preAmp(output.size());
	for (std::size_t sample = 0; sample < output.size(); ++sample)
		preAmp[sample] = amplitude[sample] > 0.0f ? static_cast<double>(output[sample]) / amplitude[sample] : 0.0;
	return preAmp;
}
}

// Development measurement, hidden: a voice reused after a natural note end keeps its filters' and DC blockers' state.
// Note 1 (Ladder, +24 dB, Res 90 %, 2 kHz, 5 ms / 300 ms) held 1 s and released until the voice ends; after a gap,
// note 2 on the same voice. The DC step note 2 starts with (mean over its first 50 ms re its settled RMS, dB), for:
// a fresh voice without and with the blocker, the reused voice as implemented, the reused voice if only the blocker
// were reset at note start (its filter state kept), and the reused voice without a blocker; then the reused voice's
// settled DC (0.5-1 s).
TEST_CASE("Kobber filter-output DC on a reused voice", "[.][kobber-dc-reuse]")
{
	constexpr int hold = 48'000, samples = 48'000;
	struct Case { const char* name; int firstNote, secondNote; float secondMorph, secondDrive; };
	std::cout << "\ncase | gap | fresh unblocked | fresh | reused | reused, blocker reset | reused unblocked | reused settled\n";
	for (const auto& [name, firstNote, secondNote, secondMorph, secondDrive] : { Case { "same patch, same note", 69, 69, 2.0f, 24.0f },
		Case { "same patch, 69 -> 45", 69, 45, 2.0f, 24.0f }, Case { "saw +24 -> square +24", 69, 69, 3.0f, 24.0f },
		Case { "saw +24 -> saw Drive 0", 69, 69, 2.0f, 0.0f } })
	{
		const auto first = measurementVoice(vekt::kobber::FilterType::ladder, 2'000.0f, 0.9f, 24.0f);
		auto second = first;
		second.morph[0] = secondMorph;
		second.drive = secondDrive;
		const auto frequency = noteHz(secondNote);
		const auto amplitude = voiceAmplitude(second, samples);
		const auto fresh = renderHeldVoice(second, secondNote, samples);
		const auto freshUnblocked = unblockedFilterOutput(fresh, amplitude);
		const auto rms = readDc(fresh, frequency, 24'000, samples).rms / amplitude.back();
		const auto db = [](double ratio) { return signedDb(ratio); };
		for (const auto gap : { 480, 96'000 })
		{
			const auto reused = renderReusedVoice(first, firstNote, hold, gap, second, secondNote, samples);
			// The reused voice's filter output over both notes' rendered samples, in the order its blocker saw them.
			auto output = reused.firstOutput;
			auto amplitudes = voiceAmplitude(first, static_cast<int>(reused.firstOutput.size()), hold);
			output.insert(output.end(), reused.second.begin(), reused.second.end());
			amplitudes.insert(amplitudes.end(), amplitude.begin(), amplitude.end());
			const auto unblocked = unblockedFilterOutput(output, amplitudes);
			const std::vector<double> secondUnblocked(unblocked.end() - samples, unblocked.end());
			vekt::dsp::DcBlocker<double> resetBlocker;
			resetBlocker.prepare(48'000.0, vekt::kobber::filterOutputDcBlockerHz);
			std::vector<double> resetAtNote(secondUnblocked.size());
			for (std::size_t sample = 0; sample < resetAtNote.size(); ++sample) resetAtNote[sample] = resetBlocker.processSample(secondUnblocked[sample]);
			std::cout << name << " | " << gap / 48 << " ms | " << db(onsetDcRatio(freshUnblocked, frequency, rms)) << " | "
				<< db(onsetDcRatio(preAmpOf(fresh, amplitude), frequency, rms)) << " | "
				<< db(onsetDcRatio(preAmpOf(reused.second, amplitude), frequency, rms)) << " | " << db(onsetDcRatio(resetAtNote, frequency, rms))
				<< " | " << db(onsetDcRatio(secondUnblocked, frequency, rms)) << " | " << db(readDc(reused.second, frequency, 24'000, samples).ratio) << "\n";
		}
	}
}

TEST_CASE("Kobber reused voice starts the same after any idle gap and settles DC-free", "[kobber][filter][filter-type][dc]")
{
	// An idle voice renders nothing and advances no state, so what a reused voice's blocker and filters carry into the
	// next note does not depend on how long it was idle; and the next note settles DC-free whatever the last one left.
	const auto first = measurementVoice(vekt::kobber::FilterType::ladder, 2'000.0f, 0.9f, 24.0f);
	auto second = first;
	second.drive = 0.0f;
	const auto shortGap = renderReusedVoice(first, 69, 24'000, 480, second, 69, 48'000);
	const auto longGap = renderReusedVoice(first, 69, 24'000, 96'000, second, 69, 48'000);
	REQUIRE(shortGap.second == longGap.second);
	const auto dc = readDc(shortGap.second, noteHz(69), 24'000, 48'000);
	INFO("settled DC " << 20.0 * std::log10(std::abs(dc.ratio)) << " dB re RMS");
	CHECK(20.0 * std::log10(std::abs(dc.ratio)) < -80.0);
}

namespace
{
// ITU-R BS.1770 K-weighting at 48 kHz (the pre-filter shelf, then the RLB high-pass), as used for LUFS: a loudness
// weighting, so filters whose extra level is low bass do not count it at full weight.
struct KWeighting
{
	struct Biquad
	{
		double b0, b1, b2, a1, a2, x1 {}, x2 {}, y1 {}, y2 {};
		double process(double x) noexcept
		{
			const auto y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
			x2 = x1;
			x1 = x;
			y2 = y1;
			y1 = y;
			return y;
		}
	};
	Biquad shelf { 1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241, 0.73248077421585 };
	Biquad highPass { 1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621 };
	double process(double x) noexcept { return highPass.process(shelf.process(x)); }
};

struct SwitchLevel
{
	double rms {}, weighted {}; // dB: broadband RMS and K-weighted level of the left channel
};

// One voice held for a second, then measured over about half a second of whole periods.
SwitchLevel measureSwitchLevel(const vekt::kobber::KobberVoiceSettings& settings, int note)
{
	constexpr double sampleRate = 48'000.0;
	vekt::kobber::KobberVoice voice;
	voice.prepare(sampleRate, 0x4d6f6e6fu);
	voice.setPanPosition(0.0f);
	voice.start(1, note, 0.8f, settings, true, false, 1);
	const auto frequency = 440.0 * std::pow(2.0, (note - 69) / 12.0);
	const auto settle = static_cast<int>(sampleRate);
	const auto window = static_cast<int>(std::round(std::round(0.5 * frequency) * sampleRate / frequency));
	KWeighting weighting;
	double energy {}, weightedEnergy {};
	for (int sample = 0; sample < settle + window; ++sample)
	{
		float left {}, right {};
		voice.render(left, right, settings, 0.0f);
		const auto weighted = weighting.process(static_cast<double>(left));
		if (sample < settle) continue;
		energy += static_cast<double>(left) * left;
		weightedEnergy += weighted * weighted;
	}
	return { 10.0 * std::log10(energy / window), 10.0 * std::log10(weightedEnergy / window) };
}
}

// Development measurement, hidden (level-matching plan, Phase 1): switching level between Ladder, SVF and K35 at every
// Mode landmark, as broadband RMS and K-weighted (BS.1770) level. Notes 36 / 48 / 60 with the cutoff at the
// fundamental, the 4th and the 16th harmonic; Resonance 0 / 50 / 90 / 100 %; Drive 0 and +12 dB. Prints, per Drive, Mode
// and Resonance, each filter minus the SVF (the middle filter) as mean [min, max] over notes and cutoffs, then the
// Ladder's high-pass against input level to separate its linear normalisation from the level-dependent part. With
// VEKT_KOBBER_DUMP set, also writes every case to switch-levels.csv.
TEST_CASE("Kobber filter switching level at every Mode", "[.][kobber-switch-gain-modes]")
{
	using vekt::kobber::FilterType;
	using namespace vekt::test::filter_prototype;
	struct Case
	{
		float drive {}, mode {}, resonance {};
		int note {};
		float ratio {};
	};
	std::vector<Case> cases;
	for (const auto drive : { 0.0f, 12.0f, 24.0f })
		for (const auto mode : { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f })
			for (const auto resonance : { 0.0f, 0.5f, 0.9f, 1.0f })
				for (const auto note : { 36, 48, 60 })
					for (const auto ratio : { 1.0f, 4.0f, 16.0f })
						cases.push_back({ drive, mode, resonance, note, ratio });
	constexpr std::array types { FilterType::ladder, FilterType::svf, FilterType::korg35 };
	const auto levels = parallelMap(cases.size() * types.size(), [&](std::size_t index)
	{
		const auto& c = cases[index / types.size()];
		const auto frequency = 440.0f * std::pow(2.0f, (static_cast<float>(c.note) - 69.0f) / 12.0f);
		return measureSwitchLevel(measurementVoice(types[index % types.size()], c.ratio * frequency, c.resonance, c.drive, c.mode), c.note);
	});
	const auto at = [&](std::size_t caseIndex, std::size_t type) { return levels[caseIndex * types.size() + type]; };
	std::unique_ptr<juce::FileOutputStream> csv;
	if (const auto* directory = std::getenv("VEKT_KOBBER_DUMP"))
	{
		const auto file = juce::File(directory).getChildFile("switch-levels.csv");
		file.deleteFile();
		csv = std::make_unique<juce::FileOutputStream>(file);
		REQUIRE(csv->openedOk());
		*csv << "drive,mode,resonance,note,cutoff_ratio,ladder_rms,svf_rms,k35_rms,ladder_k,svf_k,k35_k\n";
		for (std::size_t index = 0; index < cases.size(); ++index)
		{
			const auto& c = cases[index];
			*csv << c.drive << "," << c.mode << "," << c.resonance << "," << c.note << "," << c.ratio;
			for (std::size_t type = 0; type < types.size(); ++type) *csv << "," << juce::String(at(index, type).rms, 2);
			for (std::size_t type = 0; type < types.size(); ++type) *csv << "," << juce::String(at(index, type).weighted, 2);
			*csv << "\n";
		}
		csv->flush();
	}
	const auto summary = [](const std::vector<double>& values)
	{
		double sum {};
		for (const auto value : values) sum += value;
		return juce::String(sum / static_cast<double>(values.size()), 1) + " [" + juce::String(*std::min_element(values.begin(), values.end()), 1)
			+ ", " + juce::String(*std::max_element(values.begin(), values.end()), 1) + "]";
	};
	for (const auto drive : { 0.0f, 12.0f, 24.0f })
	{
		std::cout << "\nDrive +" << drive << " dB, minus the SVF, mean [min, max] over notes x cutoffs (dB)\n"
			<< "Mode | Res | Ladder RMS | K35 RMS | Ladder K-weighted | K35 K-weighted\n";
		for (const auto mode : { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f })
			for (const auto resonance : { 0.0f, 0.5f, 0.9f, 1.0f })
			{
				std::vector<double> ladderRms, k35Rms, ladderWeighted, k35Weighted;
				for (std::size_t index = 0; index < cases.size(); ++index)
				{
					const auto& c = cases[index];
					if (std::abs(c.drive - drive) > 1.0e-6f || std::abs(c.mode - mode) > 1.0e-6f || std::abs(c.resonance - resonance) > 1.0e-6f) continue;
					ladderRms.push_back(at(index, 0).rms - at(index, 1).rms);
					k35Rms.push_back(at(index, 2).rms - at(index, 1).rms);
					ladderWeighted.push_back(at(index, 0).weighted - at(index, 1).weighted);
					k35Weighted.push_back(at(index, 2).weighted - at(index, 1).weighted);
				}
				std::cout << mode << " | " << juce::String(100.0f * resonance, 0) << " % | " << summary(ladderRms) << " | " << summary(k35Rms)
					<< " | " << summary(ladderWeighted) << " | " << summary(k35Weighted) << "\n";
			}
	}
	// The Ladder's high-pass against input level (mixer level), cutoff at the 16th harmonic of note 48, Drive 0: if the
	// gap to the SVF were the ladder's linear normalisation r = 1 / (1 + k), it would not depend on level.
	std::cout << "\nLadder minus SVF at Mode +1 against mixer level (cutoff 16 f0, note 48, Drive 0), RMS dB\n"
		<< "Res | level 0.01 | 0.1 | 0.7\n";
	for (const auto resonance : { 0.0f, 0.5f, 0.9f, 1.0f })
	{
		std::cout << juce::String(100.0f * resonance, 0) << " %";
		for (const auto level : { 0.01f, 0.1f, 0.7f })
		{
			const auto frequency = 440.0f * std::pow(2.0f, (48.0f - 69.0f) / 12.0f);
			auto ladder = measurementVoice(FilterType::ladder, 16.0f * frequency, resonance, 0.0f, 1.0f);
			auto svf = measurementVoice(FilterType::svf, 16.0f * frequency, resonance, 0.0f, 1.0f);
			ladder.level = svf.level = { level, 0.0f, 0.0f };
			std::cout << " | " << juce::String(measureSwitchLevel(ladder, 48).rms - measureSwitchLevel(svf, 48).rms, 1);
		}
		std::cout << "\n";
	}
}

TEST_CASE("Kobber filters switch at a sensible level at Notch and HP", "[kobber][filter][filter-type][switch-gain]")
{
	// Level-matching policy (ADR 0005, 0007, 0009; 2 October 2026; the Ladder's HP, a true high-pass ladder, meets it
	// without a level lift): at Drive 0 and Resonance up to 90 %, against the SVF and
	// K-weighted, over notes 36 / 48 / 60 with the cutoff at the 1st, 4th and 16th harmonic: within 3 dB on average and
	// 6 dB in the worst case. Notch for the Ladder (K35's halfway bell boosts a harmonic only when one sits on the
	// cutoff, so it is characterised, not bounded), HP for both. Full Resonance (self-oscillation) and Drive are
	// characterised by [kobber-switch-gain-modes].
	using vekt::kobber::FilterType;
	using namespace vekt::test::filter_prototype;
	struct Case
	{
		float mode {}, resonance {}, ratio {};
		int note {};
	};
	std::vector<Case> cases;
	for (const auto mode : { 0.0f, 1.0f })
		for (const auto resonance : { 0.0f, 0.5f, 0.9f })
			for (const auto note : { 36, 48, 60 })
				for (const auto ratio : { 1.0f, 4.0f, 16.0f })
					cases.push_back({ mode, resonance, ratio, note });
	constexpr std::array types { FilterType::ladder, FilterType::svf, FilterType::korg35 };
	const auto levels = parallelMap(cases.size() * types.size(), [&](std::size_t index)
	{
		const auto& c = cases[index / types.size()];
		const auto frequency = 440.0f * std::pow(2.0f, (static_cast<float>(c.note) - 69.0f) / 12.0f);
		return measureSwitchLevel(measurementVoice(types[index % types.size()], c.ratio * frequency, c.resonance, 0.0f, c.mode), c.note).weighted;
	});
	for (const auto mode : { 0.0f, 1.0f })
		for (const auto resonance : { 0.0f, 0.5f, 0.9f })
			for (const auto type : { std::size_t { 0 }, std::size_t { 2 } })
			{
				if (mode < 0.5f && type == 2) continue;
				double sum {}, worst {};
				int count {};
				for (std::size_t index = 0; index < cases.size(); ++index)
				{
					if (std::abs(cases[index].mode - mode) > 1.0e-6f || std::abs(cases[index].resonance - resonance) > 1.0e-6f) continue;
					const auto gap = levels[index * types.size() + type] - levels[index * types.size() + 1];
					sum += gap;
					worst = std::max(worst, std::abs(gap));
					++count;
				}
				const auto mean = sum / count;
				INFO((type == 0 ? "Ladder" : "K35") << " - SVF at Mode " << mode << ", Resonance " << resonance << ": mean " << mean << " dB, worst " << worst << " dB");
				CHECK(std::abs(mean) <= 3.0);
				CHECK(worst <= 6.0);
			}
}

// Development measurement, hidden: short-term level stability of each filter's high-pass through the ordinary
// processor (default patch: one saw, no drift or detune), held note 48, Mode +1, Drive 0, at Quality 1x and 8x. 50 ms
// window RMS of the left channel over the last 2 s: range (max - min, dB) and the largest window-to-window step (dB).
// A steady saw through a time-invariant filter gives a flat level; any jitter is the filter's own.
TEST_CASE("Kobber high-pass level stability on a held note", "[.][kobber-hp-jitter]")
{
	// The startup preset (Classic Three Bass: three detuned oscillators).
	for (const auto quality : { 0.0f, 5.0f }) // Off and 8x FIR
		for (const auto type : { 0.0f, 1.0f, 2.0f })
			for (const auto cutoff : { 1'000.0f, 3'000.0f })
				for (const auto resonance : { 0.0f, 50.0f, 90.0f, 95.0f, 98.0f, 100.0f })
				{
					vekt::kobber::PluginProcessor processor;
					setParameter(processor, parameters::trackingOversampling, quality);
					setParameter(processor, parameters::filterType, type);
					setParameter(processor, parameters::filterMode, 1.0f);
					setParameter(processor, parameters::filterCutoff, cutoff);
					setParameter(processor, parameters::filterResonance, resonance);
					processor.prepareToPlay(48'000.0, 512);
					const auto output = hold(processor, 300, 48);
					constexpr int window = 2'400;
					std::vector<double> levels;
					for (int start = output.getNumSamples() - 40 * window; start + window <= output.getNumSamples(); start += window)
					{
						double energy {};
						for (int sample = start; sample < start + window; ++sample) energy += static_cast<double>(output.getSample(0, sample)) * output.getSample(0, sample);
						levels.push_back(10.0 * std::log10(energy / window + 1.0e-30));
					}
					double step {};
					for (std::size_t index = 1; index < levels.size(); ++index) step = std::max(step, std::abs(levels[index] - levels[index - 1]));
					std::cout << "Q" << quality << " " << (type < 0.5f ? "Ladder" : type < 1.5f ? "SVF" : "K35") << " HP, cutoff " << cutoff << ", Res "
						<< resonance << " %: level " << juce::String(levels.back(), 1) << " dB, range "
						<< juce::String(*std::max_element(levels.begin(), levels.end()) - *std::min_element(levels.begin(), levels.end()), 2)
						<< " dB, largest step " << juce::String(step, 2) << " dB\n";
				}
}

// Development measurement, hidden: how much of the voice's high-pass output is non-linear, through the whole processor
// (the startup preset, Classic Three Bass; held note 48; Mode +1; Drive 0; Quality 1x). Reference: the same render with
// every oscillator at 1/100 of its level, scaled back up (everything linear there). Residue re the reference, dB, and the
// reference's level, so linear ringing (in both) and non-linear products (in the residue only) can be told apart.
TEST_CASE("Kobber high-pass residue through the processor", "[.][kobber-hp-voice-residue]")
{
	const auto render = [](float type, float cutoff, float resonance, float scale, float drive, float mode)
	{
		vekt::kobber::PluginProcessor processor;
		setParameter(processor, parameters::filterType, type);
		setParameter(processor, parameters::filterMode, mode);
		setParameter(processor, parameters::filterDrive, drive);
		setParameter(processor, parameters::filterCutoff, cutoff);
		setParameter(processor, parameters::filterResonance, resonance);
		setParameter(processor, parameters::osc1Level, 100.0f * scale);
		setParameter(processor, parameters::osc2Level, 72.0f * scale);
		setParameter(processor, parameters::osc3Level, 54.0f * scale);
		processor.prepareToPlay(48'000.0, 512);
		return hold(processor, 200, 48);
	};
	// Ladder HP, Ladder LP and SVF HP at Drive 0 / +12 / +24 dB.
	struct Variant { float type, mode; const char* name; };
	for (const auto drive : { 0.0f, 12.0f, 24.0f })
	for (const auto variant : { Variant { 0.0f, 1.0f, "Ladder HP" }, Variant { 0.0f, -1.0f, "Ladder LP" }, Variant { 1.0f, 1.0f, "SVF HP" } })
		for (const auto cutoff : { 1'000.0f })
			for (const auto resonance : { 0.0f, 50.0f, 90.0f })
			{
				const auto type = variant.type;
				const auto full = render(type, cutoff, resonance, 1.0f, drive, variant.mode), quiet = render(type, cutoff, resonance, 0.01f, drive, variant.mode);
				double residue {}, energy {};
				for (int sample = full.getNumSamples() - 48'000; sample < full.getNumSamples(); ++sample)
				{
					const auto reference = 100.0 * static_cast<double>(quiet.getSample(0, sample));
					const auto delta = static_cast<double>(full.getSample(0, sample)) - reference;
					residue += delta * delta;
					energy += reference * reference;
				}
				std::cout << "Drive +" << drive << " " << variant.name << ", cutoff " << cutoff << ", Res " << resonance << " %: residue "
					<< juce::String(10.0 * std::log10(residue / energy), 1) << " dB, linear level " << juce::String(10.0 * std::log10(energy / 48'000.0), 1)
					<< " dB\n";
			}
}

// Development measurement, hidden: level transients when Mode or Resonance moves during a held note through the Ladder
// (startup preset, note 48, Drive 0, Quality 1x), the SVF for reference. 5 ms RMS windows: the loudest window in the
// 500 ms after the change and the settled level (the mean over the last 500 ms of 4 s), in dB, and their difference.
// A: Resonance 100 %, Mode jumps from LP or Notch to HP at 1 s. B: Mode 0.5, Resonance 100 -> 98 %. C: Mode +1,
// Resonance 80 -> 100 %. LFO: Mode centred at Notch with a full-depth 2 Hz LFO on Mode: the loudest 5 ms window against
// the median over the last 3 s.
TEST_CASE("Kobber Ladder transients when Mode or Resonance moves", "[.][kobber-ladder-mode-transient]")
{
	constexpr int blocks = 375, changeBlock = 94; // 4 s and 1 s at 512 samples
	constexpr int window = 240;
	const auto envelope = [](const juce::AudioBuffer<float>& output, int start, int end)
	{
		std::vector<double> levels;
		for (int sample = start; sample + window <= end; sample += window)
		{
			double energy {};
			for (int index = sample; index < sample + window; ++index) energy += static_cast<double>(output.getSample(0, index)) * output.getSample(0, index);
			levels.push_back(10.0 * std::log10(energy / window + 1.0e-30));
		}
		return levels;
	};
	struct Scenario
	{
		const char* name;
		float type, cutoff, startMode, endMode, startResonance, endResonance;
		bool lfo;
	};
	for (const auto& scenario : {
			Scenario { "A LP->HP", 0.0f, 250.0f, -1.0f, 1.0f, 100.0f, 100.0f, false }, Scenario { "A LP->HP", 0.0f, 1'000.0f, -1.0f, 1.0f, 100.0f, 100.0f, false },
			Scenario { "A Notch->HP", 0.0f, 250.0f, 0.0f, 1.0f, 100.0f, 100.0f, false }, Scenario { "A Notch->HP", 0.0f, 1'000.0f, 0.0f, 1.0f, 100.0f, 100.0f, false },
			Scenario { "B Mode 0.5, Res 100->98", 0.0f, 250.0f, 0.5f, 0.5f, 100.0f, 98.0f, false },
			Scenario { "B Mode 0.5, Res 100->98", 0.0f, 1'000.0f, 0.5f, 0.5f, 100.0f, 98.0f, false },
			Scenario { "control Mode 0.5, Res 100 held", 0.0f, 250.0f, 0.5f, 0.5f, 100.0f, 100.0f, false },
			Scenario { "control Mode 0.5, Res 98 held", 0.0f, 250.0f, 0.5f, 0.5f, 98.0f, 98.0f, false },
			Scenario { "C HP, Res 80->100", 0.0f, 250.0f, 1.0f, 1.0f, 80.0f, 100.0f, false },
			Scenario { "C HP, Res 80->100", 0.0f, 1'000.0f, 1.0f, 1.0f, 80.0f, 100.0f, false },
			Scenario { "LFO Res 100", 0.0f, 250.0f, 0.0f, 0.0f, 100.0f, 100.0f, true }, Scenario { "LFO Res 100", 0.0f, 1'000.0f, 0.0f, 0.0f, 100.0f, 100.0f, true },
			Scenario { "LFO Res 90", 0.0f, 250.0f, 0.0f, 0.0f, 90.0f, 90.0f, true }, Scenario { "LFO Res 90", 0.0f, 1'000.0f, 0.0f, 0.0f, 90.0f, 90.0f, true },
			Scenario { "A LP->HP", 1.0f, 1'000.0f, -1.0f, 1.0f, 100.0f, 100.0f, false }, Scenario { "C HP, Res 80->100", 1.0f, 1'000.0f, 1.0f, 1.0f, 80.0f, 100.0f, false },
			Scenario { "LFO Res 100", 1.0f, 1'000.0f, 0.0f, 0.0f, 100.0f, 100.0f, true } })
	{
		vekt::kobber::PluginProcessor processor;
		setParameter(processor, parameters::filterType, scenario.type);
		setParameter(processor, parameters::filterCutoff, scenario.cutoff);
		setParameter(processor, parameters::filterMode, scenario.startMode);
		setParameter(processor, parameters::filterResonance, scenario.startResonance);
		if (scenario.lfo)
		{
			setParameter(processor, parameters::lfos[0].rate, 2.0f);
			setParameter(processor, parameters::lfos[0].filterMode, 100.0f);
		}
		processor.prepareToPlay(48'000.0, 512);
		const auto output = hold(processor, blocks, 48, [&](int block)
		{
			if (block != changeBlock || scenario.lfo) return;
			setParameter(processor, parameters::filterMode, scenario.endMode);
			setParameter(processor, parameters::filterResonance, scenario.endResonance);
		});
		const auto total = output.getNumSamples();
		const auto name = juce::String(scenario.type < 0.5f ? "Ladder " : "SVF ") + scenario.name + ", cutoff " + juce::String(scenario.cutoff, 0);
		if (scenario.lfo)
		{
			auto levels = envelope(output, total - 144'000, total);
			std::sort(levels.begin(), levels.end());
			std::cout << name << ": loudest " << juce::String(levels.back(), 1) << " dB, median " << juce::String(levels[levels.size() / 2], 1)
				<< " dB, loudest - median " << juce::String(levels.back() - levels[levels.size() / 2], 1) << " dB\n";
			continue;
		}
		const auto changeSample = changeBlock * 512;
		const auto before = envelope(output, changeSample - 24'000, changeSample);
		const auto after = envelope(output, changeSample, changeSample + 24'000);
		const auto settledLevels = envelope(output, total - 24'000, total);
		double settledEnergy {}, beforeEnergy {};
		for (const auto level : settledLevels) settledEnergy += std::pow(10.0, level / 10.0);
		for (const auto level : before) beforeEnergy += std::pow(10.0, level / 10.0);
		const auto settled = 10.0 * std::log10(settledEnergy / static_cast<double>(settledLevels.size()));
		const auto beforeLevel = 10.0 * std::log10(beforeEnergy / static_cast<double>(before.size()));
		const auto loudest = *std::max_element(after.begin(), after.end());
		std::cout << name << ": before " << juce::String(beforeLevel, 1) << " dB, loudest after " << juce::String(loudest, 1) << " dB, settled "
			<< juce::String(settled, 1) << " dB, overshoot " << juce::String(loudest - std::max(settled, beforeLevel), 1) << " dB\n";
	}
}

// Development measurement, hidden: the Ladder's level against Resonance at several Modes (startup preset, note 48, Drive 0,
// cutoff 1 kHz, held 3 s, mean of the last second), dB, so a level drop toward the top of the knob shows.
TEST_CASE("Kobber Ladder level against Resonance at each Mode", "[.][kobber-ladder-resonance-level]")
{
	for (const auto mode : { 0.0f, 0.05f, 0.1f, 0.25f, 0.5f, 0.75f, 1.0f })
	{
		std::cout << "Mode " << mode << ":";
		for (const auto resonance : { 90.0f, 95.0f, 97.0f, 98.0f, 99.0f, 100.0f })
		{
			vekt::kobber::PluginProcessor processor;
			setParameter(processor, parameters::filterMode, mode);
			setParameter(processor, parameters::filterCutoff, 1'000.0f);
			setParameter(processor, parameters::filterResonance, resonance);
			processor.prepareToPlay(48'000.0, 512);
			const auto output = hold(processor, 282, 48);
			double energy {};
			for (int sample = output.getNumSamples() - 48'000; sample < output.getNumSamples(); ++sample)
				energy += static_cast<double>(output.getSample(0, sample)) * output.getSample(0, sample);
			std::cout << " " << juce::String(resonance, 0) << "%: " << juce::String(10.0 * std::log10(energy / 48'000.0), 1);
		}
		std::cout << "\n";
	}
}

TEST_CASE("Kobber Ladder high-pass keeps running under Mode modulation", "[kobber][filter][filter-type][ladder-hp]")
{
	// A 3 Hz square LFO on Mode (smoothed for 1 ms only) through a voice. At depth 1.1 Mode clamps at -1 for each LP
	// half-cycle (a sixth of a second); at 0.999 it never reaches LP. The renders differ only in the Mode transition's
	// shape while the high-pass ladder runs through both: -33 / -25 dB re the signal in the loudest 5 ms. Resetting it at LP
	// and restarting it as Mode left, even primed, rang against the warm filter at +0.5 / -2.1 dB here, so it never rests while
	// an LFO reaches Mode (ADR 0009). 0.5 s per render, all measured (one and a half LFO cycles).
	constexpr double sampleRate = 48'000.0;
	constexpr int length = 24'000;
	const auto render = [](float cutoff, float resonance, float depth)
	{
		auto settings = measurementVoice(vekt::kobber::FilterType::ladder, cutoff, resonance, 0.0f, 0.0f);
		settings.lfo[0].source.shape = vekt::kobber::LfoShape::square;
		settings.lfo[0].source.mode = vekt::kobber::LfoMode::retrigger; // Free follows the processor's clock, absent here
		settings.lfo[0].source.rateHz = 3.0f;
		settings.lfo[0].filterMode = depth;
		vekt::kobber::KobberVoice voice;
		voice.prepare(sampleRate, 0x4d6f6e6fu);
		voice.start(1, 48, 0.8f, settings, true, false, 1);
		std::vector<float> output(length);
		for (auto& sample : output)
		{
			float right {};
			voice.render(sample, right, settings, 0.0f);
		}
		return output;
	};
	for (const auto [cutoff, resonance] : { std::pair { 100.0f, 0.9f }, std::pair { 1'000.0f, 1.0f } })
	{
		const auto clamped = render(cutoff, resonance, 1.1f), unclamped = render(cutoff, resonance, 0.999f);
		double signal {}, loudestDifference {};
		for (int window = 0; window + 240 <= length; window += 240)
		{
			double difference {};
			for (int sample = window; sample < window + 240; ++sample)
			{
				const auto index = static_cast<std::size_t>(sample);
				const auto delta = static_cast<double>(clamped[index] - unclamped[index]);
				difference += delta * delta;
				signal += static_cast<double>(unclamped[index]) * unclamped[index];
			}
			loudestDifference = std::max(loudestDifference, difference / 240.0);
		}
		const auto ratio = 10.0 * std::log10(loudestDifference / (signal / length) + 1.0e-30);
		INFO("cutoff " << cutoff << ", Resonance " << resonance << ": loudest 5 ms of the difference " << ratio << " dB re the signal");
		CHECK(ratio < -12.0);
	}
}

TEST_CASE("Kobber Ladder high-pass rests only at unmodulated LP", "[kobber][filter][filter-type][ladder-hp]")
{
	// The voice resets the high-pass ladder (it stops processing) after a second at LP with no LFO on Mode, and runs it
	// again as Mode leaves; with an LFO on Mode it never rests, however long Mode dwells at LP (ADR 0009).
	constexpr double sampleRate = 48'000.0;
	const auto renderFor = [](vekt::kobber::KobberVoice& voice, const vekt::kobber::KobberVoiceSettings& settings, double seconds)
	{
		for (int sample = 0; sample < static_cast<int>(seconds * sampleRate); ++sample)
		{
			float left {}, right {};
			voice.render(left, right, settings, 0.0f);
		}
		return voice.ladderHighPassDiagnostics().samples;
	};
	{
		auto settings = measurementVoice(vekt::kobber::FilterType::ladder, 1'000.0f, 0.5f, 0.0f, -1.0f);
		vekt::kobber::KobberVoice voice;
		voice.prepare(sampleRate, 0x4d6f6e6fu);
		voice.start(1, 48, 0.8f, settings, true, false, 1);
		const auto afterRest = renderFor(voice, settings, 1.1);
		CHECK(afterRest >= 47'000u);
		CHECK(afterRest <= 48'000u);
		CHECK(renderFor(voice, settings, 0.1) == afterRest);
		settings.filterMode = 0.5f;
		CHECK(renderFor(voice, settings, 0.1) == afterRest + 4'800u);
	}
	{
		// A 0.4 Hz square LFO starting on its low half holds Mode at LP for the first 1.25 s, past the rest time.
		auto settings = measurementVoice(vekt::kobber::FilterType::ladder, 1'000.0f, 0.5f, 0.0f, 0.0f);
		settings.lfo[0].source.shape = vekt::kobber::LfoShape::square;
		settings.lfo[0].source.mode = vekt::kobber::LfoMode::retrigger; // Free follows the processor's clock, absent here
		settings.lfo[0].source.phase = 0.5f;
		settings.lfo[0].source.rateHz = 0.4f;
		settings.lfo[0].filterMode = 1.1f;
		vekt::kobber::KobberVoice voice;
		voice.prepare(sampleRate, 0x4d6f6e6fu);
		voice.start(1, 48, 0.8f, settings, true, false, 1);
		CHECK(renderFor(voice, settings, 1.2) == 57'600u);
	}
}

