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

float rawValue(vekt::mono::PluginProcessor& processor, const char* identifier)
{
	return processor.getParameters().getRawParameterValue(identifier)->load();
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
	struct Case { int fixture, quality; double sampleRate; bool multicore, svf; };
	for (const auto& [fixture, quality, sampleRate, multicore, svf] : { Case { 0, 0, 48'000.0, false, false }, Case { 1, 0, 48'000.0, false, false },
		Case { 2, 0, 48'000.0, false, false }, Case { 0, 1, 48'000.0, false, false }, Case { 1, 1, 44'100.0, false, false },
		Case { 2, 1, 96'000.0, false, false }, Case { 1, 3, 48'000.0, false, false }, Case { 1, 0, 48'000.0, true, false },
		Case { 0, 0, 48'000.0, false, true }, Case { 1, 1, 44'100.0, false, true }, Case { 2, 1, 96'000.0, false, true },
		Case { 1, 3, 48'000.0, false, true }, Case { 1, 0, 48'000.0, true, true } })
	{
		vekt::mono::PluginProcessor processor;
		setParameter(processor, parameters::quality, static_cast<float>(quality));
		setParameter(processor, parameters::multicore, multicore ? 1.0f : 0.0f);
		setParameter(processor, parameters::filterType, svf ? 1.0f : 0.0f);
		applyFixture(processor, fixture);
		processor.prepareToPlay(sampleRate, 512);
		const auto render = playChord(processor, 48 * 512);
		const auto name = "f" + juce::String(fixture) + "-q" + juce::String(quality) + "-" + juce::String(sampleRate, 0)
			+ (multicore ? "-mc" : "") + (svf ? "-svf" : "") + ".raw";
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

TEST_CASE("Mono migrates schema 9 presets to the current schema with the Ladder filter", "[mono][filter][filter-type][preset]")
{
	juce::ScopedJuceInitialiser_GUI juceInitializer;
	vekt::mono::PluginProcessor processor;
	setParameter(processor, parameters::filterCutoff, 3'210.0f);
	// Exactly what a schema-9 build saved: every current sound parameter except the filter type and K35, plus the since
	// retired Saturated Taps.
	std::vector<const char*> schema9Ids(parameters::soundParameterIds.begin(), parameters::soundParameterIds.end() - 2);
	auto preset = vekt::presets::PresetSchema::create(parameters::presetProductIdentifier, "Schema 9", processor.getParameters(), schema9Ids);
	preset.parameters.push_back({ "filterSaturatedTaps", 0.0f });
	preset.soundSchemaVersion = 9;
	setParameter(processor, parameters::filterType, 1.0f);
	REQUIRE(processor.getPresetSession().prepare(preset).wasOk());
	REQUIRE(preset.soundSchemaVersion == 12);
	REQUIRE(preset.parameters.size() == parameters::soundParameterIds.size());
	const auto written = std::find_if(preset.parameters.begin(), preset.parameters.end(), [](const auto& entry)
	{
		return entry.identifier == parameters::filterType;
	});
	REQUIRE(written != preset.parameters.end());
	REQUIRE(written->value == 0.0f);
	REQUIRE(vekt::presets::PresetSchema::apply(preset, parameters::presetProductIdentifier,
		processor.getParameters(), parameters::soundParameterIds).wasOk());
	REQUIRE(rawValue(processor, parameters::filterType) == 0.0f);
	REQUIRE(rawValue(processor, parameters::filterCutoff) == 3'210.0f);
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

TEST_CASE("Mono migrates schema 11 presets to schema 12 with K35 forced off", "[mono][filter][filter-type][k35][preset]")
{
	juce::ScopedJuceInitialiser_GUI juceInitializer;
	vekt::mono::PluginProcessor processor;
	setParameter(processor, parameters::filterType, 1.0f);
	// What a schema-11 build saved: every sound parameter but K35, here with the SVF selected.
	std::vector<const char*> schema11Ids(parameters::soundParameterIds.begin(), parameters::soundParameterIds.end() - 1);
	for (const auto strayK35 : { false, true })
	{
		INFO("stray K35 entry " << strayK35);
		auto preset = vekt::presets::PresetSchema::create(parameters::presetProductIdentifier, "Schema 11", processor.getParameters(), schema11Ids);
		preset.soundSchemaVersion = 11;
		if (strayK35) preset.parameters.push_back({ parameters::filterK35, 1.0f });
		setParameter(processor, parameters::filterK35, 1.0f); // K35 active when the old preset loads
		REQUIRE(processor.getPresetSession().prepare(preset).wasOk());
		REQUIRE(preset.soundSchemaVersion == 12);
		REQUIRE(preset.parameters.size() == parameters::soundParameterIds.size());
		REQUIRE(vekt::presets::PresetSchema::apply(preset, parameters::presetProductIdentifier,
			processor.getParameters(), parameters::soundParameterIds).wasOk());
		REQUIRE(rawValue(processor, parameters::filterK35) == 0.0f);
		REQUIRE(rawValue(processor, parameters::filterType) == 1.0f);
	}
}

TEST_CASE("Mono restores a pre-K35 project to its own Ladder or SVF even while K35 is active", "[mono][filter][filter-type][k35][state]")
{
	juce::ScopedJuceInitialiser_GUI juceInitializer;
	for (const auto savedType : { 0.0f, 1.0f })
	{
		INFO("saved filter type " << savedType);
		// A project saved before K35 existed: the current state without the filterK35 parameter.
		vekt::mono::PluginProcessor source;
		setParameter(source, parameters::filterType, savedType);
		juce::MemoryBlock saved;
		source.getStateInformation(saved);
		auto state = juce::ValueTree::readFromData(saved.getData(), saved.getSize());
		const auto parameterType = source.getParameters().state.getType();
		auto parameterTree = state.hasType(parameterType) ? state : state.getChildWithName(parameterType);
		REQUIRE(parameterTree.isValid());
		const auto k35 = parameterTree.getChildWithProperty("id", parameters::filterK35);
		REQUIRE(k35.isValid());
		parameterTree.removeChild(k35, nullptr);
		juce::MemoryOutputStream stream;
		state.writeToStream(stream);
		// Loaded into a processor whose K35 is on and whose Filter Type differs.
		vekt::mono::PluginProcessor target;
		setParameter(target, parameters::filterK35, 1.0f);
		setParameter(target, parameters::filterType, 1.0f - savedType);
		target.setStateInformation(stream.getData(), static_cast<int>(stream.getDataSize()));
		REQUIRE(rawValue(target, parameters::filterK35) == 0.0f);
		REQUIRE(rawValue(target, parameters::filterType) == savedType);
	}
	// A project saved with K35 on restores it.
	vekt::mono::PluginProcessor source, target;
	setParameter(source, parameters::filterK35, 1.0f);
	juce::MemoryBlock saved;
	source.getStateInformation(saved);
	target.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
	REQUIRE(rawValue(target, parameters::filterK35) == 1.0f);
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

TEST_CASE("Mono K35 output stage removes the DC offset and keeps the low end", "[mono][filter][filter-type][k35]")
{
	// The blocker alone: first order at 5 Hz, about -3 dB there and within 0.3 dB from 20 Hz up.
	for (const auto sampleRate : { 48'000.0, 384'000.0 })
	{
		const auto gainAt = [sampleRate](double frequency)
		{
			vekt::dsp::DcBlocker<double> blocker;
			blocker.prepare(sampleRate, vekt::mono::korg35DcBlockerHz);
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
	// The output stage on K35's own output: a driven band-limited 100 Hz saw (whole periods at 96 kHz) leaves the bare
	// filter with a DC offset of tens of dB below its RMS; after the blocker it is gone. (The voice applies the blocker
	// to the filter's output only, so the loop, which feeds back the unblocked limiter output, is unchanged.)
	constexpr double sampleRate = 96'000.0;
	constexpr int period = 960;
	vekt::mono::NonlinearTptKorg35 filter;
	filter.prepare(sampleRate);
	vekt::mono::NonlinearTptKorg35Settings settings;
	settings.cutoffHz = 2'000.0;
	settings.feedback = vekt::mono::korg35Feedback(0.9);
	settings.knee = vekt::mono::korg35Knee;
	settings.driveDecibels = 24.0;
	vekt::dsp::DcBlocker<double> blocker;
	blocker.prepare(sampleRate, vekt::mono::korg35DcBlockerHz);
	double rawMean {}, rawEnergy {}, blockedMean {}, blockedEnergy {};
	constexpr int settle = 200 * period, measured = 100 * period;
	for (int sample = 0; sample < settle + measured; ++sample)
	{
		double saw {};
		for (int harmonic = 1; harmonic * 100 < 20'000; ++harmonic)
			saw += (harmonic % 2 == 1 ? 1.0 : -1.0) * std::sin(2.0 * std::numbers::pi * harmonic * (sample % period) / period) / harmonic;
		const auto raw = filter.process(2.0 / std::numbers::pi * saw, settings);
		const auto blocked = blocker.processSample(raw);
		if (sample < settle) continue;
		rawMean += raw;
		rawEnergy += raw * raw;
		blockedMean += blocked;
		blockedEnergy += blocked * blocked;
	}
	const auto dcDb = [](double mean, double energy) { return 20.0 * std::log10(std::abs(mean / measured) / std::sqrt(energy / measured) + 1.0e-300); };
	INFO("DC re RMS: raw " << dcDb(rawMean, rawEnergy) << " dB, blocked " << dcDb(blockedMean, blockedEnergy) << " dB");
	CHECK(dcDb(rawMean, rawEnergy) > -40.0);
	CHECK(dcDb(blockedMean, blockedEnergy) < -100.0);
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
