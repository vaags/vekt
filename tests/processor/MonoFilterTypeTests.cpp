#include <vekt/mono/PluginProcessor.h>

#include "MonoVoice.h"

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
namespace parameters = vekt::mono::parameters;

void setParameter(vekt::mono::PluginProcessor& processor, const char* identifier, float value)
{
	auto* parameter = processor.getParameters().getParameter(identifier);
	parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

// Holds `note` (or plays nothing if note < 0) for `blocks` 512-sample blocks at 48 kHz; beforeBlock runs before each
// block, so parameter changes land on block boundaries.
juce::AudioBuffer<float> hold(vekt::mono::PluginProcessor& processor, int blocks, int note,
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
			if (first.getSample(channel, sample) != second.getSample(channel, sample)) return false;
	return true;
}

// Plays a three-note chord for most of `samples` in 512-sample blocks, then releases it.
juce::AudioBuffer<float> playChord(vekt::mono::PluginProcessor& processor, int samples)
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
void applyFixture(vekt::mono::PluginProcessor& processor, int fixture)
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

// Development check, hidden from normal runs: with VEKT_MONO_DUMP set to a directory, writes raw renders of the
// fixtures at several rates and qualities, to compare two builds bit for bit.
TEST_CASE("Mono dumps filter fixture renders", "[.][mono-dump]")
{
	const auto* directory = std::getenv("VEKT_MONO_DUMP");
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
		vekt::mono::PluginProcessor processor;
		setParameter(processor, parameters::quality, static_cast<float>(quality));
		setParameter(processor, parameters::multicore, multicore ? 1.0f : 0.0f);
		setParameter(processor, parameters::filterType, svf ? 1.0f : 0.0f);
		setParameter(processor, parameters::filterK35, k35 ? 1.0f : 0.0f);
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

TEST_CASE("Mono filter type is an appended sound parameter that defaults to Ladder", "[mono][filter][filter-type][parameters]")
{
	vekt::mono::PluginProcessor processor;
	auto* parameter = dynamic_cast<juce::AudioParameterChoice*>(processor.getParameters().getParameter(parameters::filterType));
	REQUIRE(parameter != nullptr);
	REQUIRE(parameter->choices == juce::StringArray { "Ladder", "SVF" });
	REQUIRE(parameter->getIndex() == 0);
	// Registered after every earlier parameter (only K35, schema 12, follows it), so existing host automation indices
	// do not move.
	const auto& all = processor.getParameters().processor.getParameters();
	REQUIRE(all[all.size() - 2] == parameter);
	REQUIRE(parameters::soundParameterIds[parameters::soundParameterIds.size() - 2] == parameters::filterType);
	REQUIRE(parameters::schema10ParameterIds.size() == 1);
}

TEST_CASE("Mono SVF renders its own finite sound", "[mono][filter][filter-type]")
{
	const auto render = [](float type)
	{
		vekt::mono::PluginProcessor processor;
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

TEST_CASE("Mono voices adopt a filter type chosen while silent without a transition", "[mono][filter][filter-type]")
{
	vekt::mono::PluginProcessor fromStart;
	setParameter(fromStart, parameters::filterType, 1.0f);
	fromStart.prepareToPlay(48'000.0, 512);
	hold(fromStart, 4, -1);
	const auto expected = hold(fromStart, 16, 45);

	vekt::mono::PluginProcessor switched;
	switched.prepareToPlay(48'000.0, 512);
	hold(switched, 4, -1, [&switched](int block) { if (block == 2) setParameter(switched, parameters::filterType, 1.0f); });
	REQUIRE(identical(hold(switched, 16, 45), expected));
}

TEST_CASE("Mono filter type switches under a held note without a click", "[mono][filter][filter-type]")
{
	for (const auto [from, to] : { std::pair { 0.0f, 1.0f }, std::pair { 1.0f, 0.0f } })
	{
		INFO("from " << from << " to " << to);
		vekt::mono::PluginProcessor processor;
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

TEST_CASE("Mono SVF Mode sweeps smoothly and changes the held sound", "[mono][filter][filter-type][svf-mode]")
{
	// The SVF reads the same smoothed Mode as the ladder: a Mode change ramps from the previous response.
	vekt::mono::PluginProcessor lowPass, swept;
	for (auto* processor : { &lowPass, &swept })
	{
		setParameter(*processor, parameters::filterType, 1.0f);
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

// Development audition, hidden from normal runs: with VEKT_MONO_DUMP set to a directory, renders a held A2 saw with a
// slow filter-envelope sweep at full Resonance through Ladder and SVF at Drive 0, +12 and +24 dB, as 32-bit float
// WAV files each normalised to -20 dBFS RMS: for comparing character, since the switching gain is not calibrated yet.
TEST_CASE("Mono renders Ladder and SVF audition files", "[.][mono-audition]")
{
	const auto* directory = std::getenv("VEKT_MONO_DUMP");
	REQUIRE(directory != nullptr);
	const juce::File folder(directory);
	REQUIRE(folder.createDirectory().wasOk());
	for (const auto type : { 0.0f, 1.0f })
		for (const auto drive : { 0.0f, 12.0f, 24.0f })
		{
			vekt::mono::PluginProcessor processor;
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
TEST_CASE("Mono closed-filter leakage", "[.][mono-closed]")
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
						vekt::mono::PluginProcessor processor;
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
vekt::mono::MonoVoiceSettings measurementVoice(vekt::mono::FilterType type, float cutoff, float resonance, float drive,
	float mode = -1.0f)
{
	vekt::mono::MonoVoiceSettings settings {};
	settings.range = { 1.0f, 1.0f, 1.0f };
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

VoiceLevel measureVoice(const vekt::mono::MonoVoiceSettings& settings, int note)
{
	constexpr double sampleRate = 48'000.0;
	vekt::mono::MonoVoice voice;
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
TEST_CASE("Mono Ladder to SVF switching level", "[.][mono-switch-gain]")
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
					const auto ladder = measureVoice(measurementVoice(vekt::mono::FilterType::ladder, cutoff, resonance, drive), note);
					const auto svf = measureVoice(measurementVoice(vekt::mono::FilterType::svf, cutoff, resonance, drive), note);
					std::cout << "  " << juce::String(cutoff, 0) << "/" << note << " " << juce::String(svf.rms - ladder.rms, 1) << "/"
						<< juce::String(svf.fundamental - ladder.fundamental, 1);
				}
			std::cout << "\n";
		}
	}
}

TEST_CASE("Mono Ladder and SVF switch at a sensible level at Drive 0", "[mono][filter][filter-type][switch-gain]")
{
	// ADR 0006's switching-gain policy on the controlled saw: under 6 dB everywhere, and within 3 dB below full
	// Resonance (where the Ladder's own resonant peak adds level the fundamental does not show).
	for (const auto resonance : { 0.0f, 0.5f, 0.9f, 1.0f })
		for (const auto note : { 36, 48 })
		{
			const auto ladder = measureVoice(measurementVoice(vekt::mono::FilterType::ladder, 1'200.0f, resonance, 0.0f), note);
			const auto svf = measureVoice(measurementVoice(vekt::mono::FilterType::svf, 1'200.0f, resonance, 0.0f), note);
			INFO("Resonance " << resonance << ", note " << note << ": SVF - Ladder " << svf.rms - ladder.rms << " dB RMS");
			CHECK(std::abs(svf.rms - ladder.rms) < 6.0);
			if (resonance < 1.0f) CHECK(std::abs(svf.rms - ladder.rms) <= 3.0);
		}
}

// Development measurement, hidden: the switching level per Mode at Drive 0, since the SVF's Resonance trim was
// derived from LP. SVF minus Ladder, RMS dB, for notes 36 and 48 at cutoff 1.2 kHz, and for HP also with the notes
// well above a 40 Hz cutoff, so the HP passband rather than its stopband slope is compared.
TEST_CASE("Mono Ladder to SVF switching level per Mode", "[.][mono-switch-gain-mode]")
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
				const auto ladder = measureVoice(measurementVoice(vekt::mono::FilterType::ladder, cutoff, resonance, 0.0f, mode), note);
				const auto svf = measureVoice(measurementVoice(vekt::mono::FilterType::svf, cutoff, resonance, 0.0f, mode), note);
				std::cout << " " << juce::String(svf.rms - ladder.rms, 1) << "/" << juce::String(svf.fundamental - ladder.fundamental, 1);
			}
		}
		std::cout << "\n";
	}
}

// Development measurement, hidden: K35 switching level against the Ladder and the SVF (K35 minus each, broadband RMS
// dB), in LP, over Resonance x Drive for notes 36 / 48 at cutoffs 300 Hz / 1.2 kHz / 5 kHz (ADR 0007). Printed per
// Drive as the mean and range over cutoffs and notes.
TEST_CASE("Mono K35 switching level", "[.][mono-switch-gain-k35]")
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
					const auto k35 = measureVoice(measurementVoice(vekt::mono::FilterType::korg35, cutoff, resonance, drive), note);
					ladderGap.push_back(k35.rms - measureVoice(measurementVoice(vekt::mono::FilterType::ladder, cutoff, resonance, drive), note).rms);
					svfGap.push_back(k35.rms - measureVoice(measurementVoice(vekt::mono::FilterType::svf, cutoff, resonance, drive), note).rms);
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

TEST_CASE("Mono K35 switches at a sensible level at Drive 0", "[mono][filter][filter-type][k35][switch-gain]")
{
	// ADR 0006's switching policy for K35 against the Ladder (under 6 dB everywhere, within 3 dB below full Resonance),
	// and within 1.5 dB of the SVF, whose Resonance trim it shares (ADR 0007).
	for (const auto resonance : { 0.0f, 0.5f, 0.9f, 1.0f })
		for (const auto note : { 36, 48 })
		{
			const auto k35 = measureVoice(measurementVoice(vekt::mono::FilterType::korg35, 1'200.0f, resonance, 0.0f), note);
			const auto ladder = measureVoice(measurementVoice(vekt::mono::FilterType::ladder, 1'200.0f, resonance, 0.0f), note);
			const auto svf = measureVoice(measurementVoice(vekt::mono::FilterType::svf, 1'200.0f, resonance, 0.0f), note);
			INFO("Resonance " << resonance << ", note " << note << ": K35 - Ladder " << k35.rms - ladder.rms << " dB, K35 - SVF " << k35.rms - svf.rms << " dB");
			CHECK(std::abs(k35.rms - ladder.rms) < 6.0);
			if (resonance < 1.0f) CHECK(std::abs(k35.rms - ladder.rms) <= 3.0);
			CHECK(std::abs(k35.rms - svf.rms) <= 1.5);
		}
}

// K35 (ADR 0007): an appended override on top of the Ladder/SVF filter type.
TEST_CASE("Mono K35 is an appended override that defaults off and leaves Filter Type's mapping unchanged", "[mono][filter][filter-type][k35][parameters]")
{
	vekt::mono::PluginProcessor processor;
	auto* k35 = dynamic_cast<juce::AudioParameterBool*>(processor.getParameters().getParameter(parameters::filterK35));
	REQUIRE(k35 != nullptr);
	REQUIRE_FALSE(k35->get());
	REQUIRE(static_cast<juce::AudioProcessorParameter*>(k35)->getDefaultValue() < 0.5f);
	const auto& all = processor.getParameters().processor.getParameters();
	REQUIRE(all.getLast() == k35);
	REQUIRE(parameters::soundParameterIds.back() == parameters::filterK35);
	REQUIRE(parameters::schema12ParameterIds.size() == 1);
	// Host automation of Filter Type keeps its exact two-state normalised mapping.
	auto* type = dynamic_cast<juce::AudioParameterChoice*>(processor.getParameters().getParameter(parameters::filterType));
	REQUIRE(type != nullptr);
	REQUIRE(type->choices == juce::StringArray { "Ladder", "SVF" });
	REQUIRE(static_cast<juce::AudioProcessorParameter*>(type)->getNumSteps() == 2);
	type->setValueNotifyingHost(1.0f);
	REQUIRE(type->getIndex() == 1);
	type->setValueNotifyingHost(0.0f);
	REQUIRE(type->getIndex() == 0);
	REQUIRE(type->convertTo0to1(1.0f) == 1.0f);
}

TEST_CASE("Mono K35 renders its own sound; Filter Type under it is inaudible and revealed when K35 turns off", "[mono][filter][filter-type][k35]")
{
	const auto render = [](float k35, float type, int typeChangeBlock = -1, int k35OffBlock = -1)
	{
		vekt::mono::PluginProcessor processor;
		setParameter(processor, parameters::filterK35, k35);
		setParameter(processor, parameters::filterType, type);
		setParameter(processor, parameters::filterResonance, 80.0f);
		processor.prepareToPlay(48'000.0, 512);
		return hold(processor, 32, 45, [&](int block)
		{
			if (block == typeChangeBlock) setParameter(processor, parameters::filterType, 1.0f);
			if (block == k35OffBlock) setParameter(processor, parameters::filterK35, 0.0f);
		});
	};
	const auto k35 = render(1.0f, 0.0f), ladder = render(0.0f, 0.0f), svf = render(0.0f, 1.0f);
	const auto samples = k35.getNumSamples();
	for (int channel = 0; channel < 2; ++channel)
		for (int sample = 0; sample < samples; ++sample) REQUIRE(std::isfinite(k35.getSample(channel, sample)));
	REQUIRE(k35.getRMSLevel(0, 0, samples) > 0.01f);
	REQUIRE_FALSE(identical(k35, ladder));
	REQUIRE_FALSE(identical(k35, svf));
	// Under K35 the Filter Type makes no difference at all, set from the start or automated mid-note.
	REQUIRE(identical(render(1.0f, 1.0f), k35));
	REQUIRE(identical(render(1.0f, 0.0f, 8), k35));
	// Filter Type automated to SVF under K35, then K35 off: exactly as if SVF had been underneath all along.
	REQUIRE(identical(render(1.0f, 0.0f, 8, 16), render(1.0f, 1.0f, -1, 16)));
	// And that is the SVF, not the Ladder: switching K35 off from Ladder underneath sounds different.
	REQUIRE_FALSE(identical(render(1.0f, 0.0f, -1, 16), render(1.0f, 1.0f, -1, 16)));
}

TEST_CASE("Mono K35 switches under a held note without a click", "[mono][filter][filter-type][k35]")
{
	// Ladder or SVF underneath, K35 on and then back off: each switch declicks like a Filter Type change.
	for (const auto type : { 0.0f, 1.0f })
		for (const auto [from, to] : { std::pair { 0.0f, 1.0f }, std::pair { 1.0f, 0.0f } })
		{
			INFO("filter type " << type << ", K35 " << from << " -> " << to);
			vekt::mono::PluginProcessor processor;
			setParameter(processor, parameters::filterType, type);
			setParameter(processor, parameters::filterK35, from);
			setParameter(processor, parameters::filterCutoff, 1'500.0f);
			setParameter(processor, parameters::filterResonance, 30.0f);
			processor.prepareToPlay(48'000.0, 512);
			constexpr int switchBlock = 24;
			const auto output = hold(processor, 48, 45, [&processor, to](int block)
			{
				if (block == switchBlock) setParameter(processor, parameters::filterK35, to);
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

TEST_CASE("Mono K35 stays finite under hostile modulation through the processor", "[mono][filter][filter-type][k35]")
{
	for (const auto quality : { 0.0f, 3.0f })
		for (const auto multicore : { 0.0f, 1.0f })
		{
			INFO("quality " << quality << ", multicore " << multicore);
			vekt::mono::PluginProcessor processor;
			setParameter(processor, parameters::quality, quality);
			setParameter(processor, parameters::multicore, multicore);
			setParameter(processor, parameters::filterK35, 1.0f);
			setParameter(processor, parameters::filterResonance, 100.0f);
			setParameter(processor, parameters::filterDrive, 24.0f);
			setParameter(processor, parameters::unison, 2.0f);
			setParameter(processor, parameters::noiseType, 2.0f);
			setParameter(processor, parameters::noiseLevel, 60.0f);
			setParameter(processor, parameters::lfos[0].rate, 7.0f);
			setParameter(processor, parameters::lfos[0].filter, 4.0f);
			setParameter(processor, parameters::lfos[0].drive, 24.0f);
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
std::vector<float> renderHeldVoice(const vekt::mono::MonoVoiceSettings& settings, int note, int samples, int releaseAt = -1)
{
	vekt::mono::MonoVoice voice;
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
	const auto a = std::exp(-2.0 * std::numbers::pi * vekt::mono::filterOutputDcBlockerHz / 48'000.0);
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
	vekt::mono::WidthOscillatorState state;
	double phase {};
	for (auto& value : saw)
	{
		phase += frequency / 48'000.0;
		phase -= std::floor(phase);
		value = level * vekt::mono::renderWidthOscillator(state, static_cast<float>(phase), static_cast<float>(frequency), 48'000.0, morph, pulseWidth, false);
	}
	return saw;
}

// A bare filter (Ladder, SVF low-pass or K35, from rest, untrimmed) at 48 kHz: what the voice's DC blocker receives.
std::vector<float> renderBareFilter(vekt::mono::FilterType type, float cutoff, float resonance, float drive, const std::vector<float>& input)
{
	std::vector<float> y(input.size());
	vekt::mono::NonlinearTptLadder ladder;
	vekt::mono::NonlinearTptSvf svf;
	vekt::mono::NonlinearTptKorg35 korg;
	ladder.prepare(48'000.0);
	svf.prepare(48'000.0);
	korg.prepare(48'000.0);
	const vekt::mono::NonlinearTptLadderSettings ladderSettings { cutoff, resonance, drive, false, 0.0f, -1.0f };
	const vekt::mono::NonlinearTptSvfSettings svfSettings { cutoff, vekt::mono::svfDamping(resonance), drive, vekt::mono::svfKnee,
		vekt::mono::svfDampingCurve };
	vekt::mono::NonlinearTptKorg35Settings korgSettings;
	korgSettings.cutoffHz = cutoff;
	korgSettings.feedback = vekt::mono::korg35Feedback(resonance);
	korgSettings.driveDecibels = drive;
	korgSettings.knee = vekt::mono::korg35Knee;
	for (std::size_t n = 0; n < y.size(); ++n)
		y[n] = type == vekt::mono::FilterType::ladder ? ladder.processCoupled(input[n], ladderSettings)
			: type == vekt::mono::FilterType::svf ? static_cast<float>(svf.process(input[n], svfSettings).lowPass)
			: static_cast<float>(korg.process(input[n], korgSettings));
	return y;
}

// The voice's amplitude (amp envelope x allocation fade) as MonoVoice computes it, for measurementVoice settings.
std::vector<float> voiceAmplitude(const vekt::mono::MonoVoiceSettings& settings, int samples, int releaseAt = -1)
{
	vekt::mono::ContourEnvelope amp;
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
TEST_CASE("Mono Ladder and SVF output DC", "[.][mono-dc]")
{
	constexpr int samples = 72'000;
	for (const auto type : { vekt::mono::FilterType::ladder, vekt::mono::FilterType::svf })
		for (const auto drive : { 0.0f, 12.0f, 24.0f })
		{
			std::cout << "\n" << (type == vekt::mono::FilterType::ladder ? "Ladder" : "SVF") << " Drive +" << drive
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
TEST_CASE("Mono Ladder and SVF output DC sources", "[.][mono-dc-source]")
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
			vekt::mono::WidthOscillatorState state;
			std::vector<float> y(48'000);
			double phase {};
			const auto frequency = noteHz(45);
			for (auto& value : y)
			{
				phase += frequency / 48'000.0;
				phase -= std::floor(phase);
				value = vekt::mono::renderWidthOscillator(state, static_cast<float>(phase), static_cast<float>(frequency), 48'000.0, shape.morph, shape.width, zeroCentred);
			}
			std::cout << " " << signedDb(readDc(y, frequency, 4'800, 48'000).ratio);
		}
		std::cout << "\n";
	}
	for (const auto type : { vekt::mono::FilterType::ladder, vekt::mono::FilterType::svf })
	{
		std::cout << "\n" << (type == vekt::mono::FilterType::ladder ? "Ladder" : "SVF")
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
				settings.widthDcPolicy = vekt::mono::WidthDcPolicy::zeroCentered;
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
TEST_CASE("Mono Ladder and SVF output DC through the processor", "[.][mono-dc-processor]")
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
					vekt::mono::PluginProcessor processor;
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
TEST_CASE("Mono Ladder and SVF output DC at note on and off", "[.][mono-dc-thump]")
{
	constexpr int samples = 72'000, releaseAt = 48'000, window = 2'400;
	struct Envelope { float attack, release; const char* name; };
	for (const auto type : { vekt::mono::FilterType::ladder, vekt::mono::FilterType::svf })
		for (const auto drive : { 0.0f, 24.0f })
		{
			std::cout << "\n" << (type == vekt::mono::FilterType::ladder ? "Ladder" : "SVF") << ", Drive +" << drive
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
					blocker.prepare(48'000.0, vekt::mono::filterOutputDcBlockerHz);
					voiceBlocker.prepare(48'000.0, vekt::mono::filterOutputDcBlockerHz);
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
TEST_CASE("Mono Ladder, SVF and K35 DC polarity and level", "[.][mono-dc-polarity]")
{
	constexpr int samples = 72'000;
	const auto frequency = noteHz(45);
	const auto saw = bandLimitedSaw(45, samples, 0.7f);
	const auto run = [&](int type, float resonance, float drive, float gain)
	{
		auto input = saw;
		for (auto& value : input) value *= gain;
		return renderBareFilter(static_cast<vekt::mono::FilterType>(type), 2'000.0f, resonance, drive, input);
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
TEST_CASE("Mono Ladder output DC on short notes", "[.][mono-dc-short]")
{
	constexpr int samples = 48'000, window = 2'400;
	std::cout << "\nLadder +24 dB, Res 90 %, 2 kHz, 5 ms / 5 ms | note | length | unblocked | 5 Hz | 10 Hz | 20 Hz\n";
	for (const auto note : { 45, 69, 81 })
	{
		auto settings = measurementVoice(vekt::mono::FilterType::ladder, 2'000.0f, 0.9f, 24.0f);
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

TEST_CASE("Mono filter-output DC blocker keeps the low end", "[mono][filter][filter-type][dc]")
{
	// First order at 5 Hz (ADR 0008): about -3 dB there and within 0.3 dB from 20 Hz up, at the base and at the 8x rate.
	for (const auto sampleRate : { 48'000.0, 384'000.0 })
	{
		const auto gainAt = [sampleRate](double frequency)
		{
			vekt::dsp::DcBlocker<double> blocker;
			blocker.prepare(sampleRate, vekt::mono::filterOutputDcBlockerHz);
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

TEST_CASE("Mono removes every filter's DC at the filter output", "[mono][filter][filter-type][dc]")
{
	// A driven saw into each filter's stress case: the bare filter generates tens of dB of DC (its saturation on a
	// waveform without half-wave symmetry), and the held voice's settled output carries none of it.
	struct Case { vekt::mono::FilterType type; float cutoff, resonance; double bareAbove; };
	for (const auto& [type, cutoff, resonance, bareAbove] : { Case { vekt::mono::FilterType::ladder, 300.0f, 0.9f, -15.0 },
		Case { vekt::mono::FilterType::svf, 300.0f, 0.5f, -35.0 }, Case { vekt::mono::FilterType::korg35, 2'000.0f, 0.9f, -40.0 } })
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

TEST_CASE("Mono raw pulse keeps its DC into the filter and none at the voice output", "[mono][filter][filter-type][dc][width]")
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
	CHECK(db(readDc(renderBareFilter(vekt::mono::FilterType::ladder, 1'000.0f, 0.5f, 12.0f, input), noteHz(45), 24'000, samples).ratio) > -15.0);
	auto settings = measurementVoice(vekt::mono::FilterType::ladder, 1'000.0f, 0.5f, 12.0f);
	settings.morph[0] = 3.0f;
	settings.pulseWidth[0] = 25.0f;
	const auto raw = renderHeldVoice(settings, 45, samples);
	settings.widthDcPolicy = vekt::mono::WidthDcPolicy::zeroCentered;
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

TEST_CASE("Mono filter-output DC blocker has mostly settled by the end of a 100 ms note", "[mono][filter][filter-type][dc]")
{
	// The blocker's time constant is 32 ms, so the DC step at note on is only partly removed on very short notes (20 ms
	// notes keep most of it: a known limit, characterised by [mono-dc-short]). By 100 ms, when a fast release would expose
	// it, it is at least 20 dB down. Worst held case: Ladder, +24 dB, Res 90 %, 2 kHz, note 69, 0.5 ms attack.
	constexpr int samples = 48'000;
	auto settings = measurementVoice(vekt::mono::FilterType::ladder, 2'000.0f, 0.9f, 24.0f);
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

ReusedVoiceRender renderReusedVoice(const vekt::mono::MonoVoiceSettings& first, int firstNote, int holdSamples, int gapSamples,
	const vekt::mono::MonoVoiceSettings& second, int secondNote, int secondSamples)
{
	vekt::mono::MonoVoice voice;
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
TEST_CASE("Mono filter-output DC on a reused voice", "[.][mono-dc-reuse]")
{
	constexpr int hold = 48'000, samples = 48'000;
	struct Case { const char* name; int firstNote, secondNote; float secondMorph, secondDrive; };
	std::cout << "\ncase | gap | fresh unblocked | fresh | reused | reused, blocker reset | reused unblocked | reused settled\n";
	for (const auto& [name, firstNote, secondNote, secondMorph, secondDrive] : { Case { "same patch, same note", 69, 69, 2.0f, 24.0f },
		Case { "same patch, 69 -> 45", 69, 45, 2.0f, 24.0f }, Case { "saw +24 -> square +24", 69, 69, 3.0f, 24.0f },
		Case { "saw +24 -> saw Drive 0", 69, 69, 2.0f, 0.0f } })
	{
		const auto first = measurementVoice(vekt::mono::FilterType::ladder, 2'000.0f, 0.9f, 24.0f);
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
			resetBlocker.prepare(48'000.0, vekt::mono::filterOutputDcBlockerHz);
			std::vector<double> resetAtNote(secondUnblocked.size());
			for (std::size_t sample = 0; sample < resetAtNote.size(); ++sample) resetAtNote[sample] = resetBlocker.processSample(secondUnblocked[sample]);
			std::cout << name << " | " << gap / 48 << " ms | " << db(onsetDcRatio(freshUnblocked, frequency, rms)) << " | "
				<< db(onsetDcRatio(preAmpOf(fresh, amplitude), frequency, rms)) << " | "
				<< db(onsetDcRatio(preAmpOf(reused.second, amplitude), frequency, rms)) << " | " << db(onsetDcRatio(resetAtNote, frequency, rms))
				<< " | " << db(onsetDcRatio(secondUnblocked, frequency, rms)) << " | " << db(readDc(reused.second, frequency, 24'000, samples).ratio) << "\n";
		}
	}
}

TEST_CASE("Mono reused voice starts the same after any idle gap and settles DC-free", "[mono][filter][filter-type][dc]")
{
	// An idle voice renders nothing and advances no state, so what a reused voice's blocker and filters carry into the
	// next note does not depend on how long it was idle; and the next note settles DC-free whatever the last one left.
	const auto first = measurementVoice(vekt::mono::FilterType::ladder, 2'000.0f, 0.9f, 24.0f);
	auto second = first;
	second.drive = 0.0f;
	const auto shortGap = renderReusedVoice(first, 69, 24'000, 480, second, 69, 48'000);
	const auto longGap = renderReusedVoice(first, 69, 24'000, 96'000, second, 69, 48'000);
	REQUIRE(shortGap.second == longGap.second);
	const auto dc = readDc(shortGap.second, noteHz(69), 24'000, 48'000);
	INFO("settled DC " << 20.0 * std::log10(std::abs(dc.ratio)) << " dB re RMS");
	CHECK(20.0 * std::log10(std::abs(dc.ratio)) < -80.0);
}
