// Cost and aliasing of Mono's Width oscillator. Times one oscillator of the production table renderer
// against the exact additive reference, then the whole processor per
// Quality, and measures inharmonic output power for a narrow high pulse (the whistle setup).
#include "MonoAdditiveOscillator.h"

#include <vekt/mono/PluginProcessor.h>

#include <juce_events/juce_events.h>

#include <chrono>
#include <iomanip>
#include <iostream>

namespace
{
void setParameter(vekt::mono::PluginProcessor& processor, const char* identifier, float value)
{
	auto* parameter = processor.getParameters().getParameter(identifier);
	parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

// The Tracking Oversampling choice for a factor: Off, 2x IIR or 4x FIR.
float qualityChoice(int factor) { return factor == 4 ? 4.0f : factor == 2 ? 1.0f : 0.0f; }
}

int main()
{
	using Policy = vekt::audio_lab::MonoAdditiveOscillator::Policy;
	constexpr double rate = 48'000.0;
	constexpr int samples = 96'000;
	vekt::audio_lab::MonoAdditiveOscillator additive;
	additive.setHostRate(rate);
	additive.setPolicy(Policy::fourBandsPerOctave);
	volatile float sink {};
	std::cout << std::fixed << std::setprecision(1);
	// Static Morph/Width, then a 2 Hz sine LFO on Morph (0..3) or Width (5..95 %).
	for (int modulation = 0; modulation < 3; ++modulation)
		for (const auto pitch : { 30.0f, 55.0f, 220.0f, 880.0f, 2700.0f })
			for (int mode = 1; mode < 3; ++mode)
			{
				if (modulation > 0 && mode != 1) continue;
				vekt::mono::WidthOscillatorState state;
				const auto increment = static_cast<float>(pitch / rate);
				float phase {};
				const auto start = std::chrono::steady_clock::now();
				for (int sample = 0; sample < samples; ++sample)
				{
					phase += increment;
					phase -= std::floor(phase);
					const auto lfo = std::sin(2.0f * std::numbers::pi_v<float> * 2.0f * static_cast<float>(sample) / static_cast<float>(rate));
					const auto morph = modulation == 1 ? 1.5f + 1.5f * lfo : 2.5f;
					const auto width = modulation == 2 ? 50.0f + 45.0f * lfo : 83.0f;
					sink = sink + (mode == 1 ? vekt::mono::renderWidthOscillator(state, phase, pitch, rate, morph, width, false)
						: additive.sample(phase, pitch, morph, width));
				}
				const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
				static constexpr const char* modes[] { "", "production", "additive 4bpo" };
				static constexpr const char* modulations[] { "static   ", "morph LFO", "width LFO" };
				std::cout << "oscillator 1x " << modulations[modulation] << std::setw(7) << pitch << " Hz " << std::setw(13)
					<< modes[mode] << std::setw(9) << seconds * 1.0e9 / samples << " ns/sample " << std::setw(6)
					<< 100.0 * seconds / (samples / rate) << " % core\n";
			}
	std::cout << "tables " << vekt::mono::WidthWavetable::instance().bytes() << " bytes\n";

	juce::ScopedJuceInitialiser_GUI juceInitialiser;
	// Whole processor with its default patch, one held note.
	for (const auto factor : { 1, 2, 4 })
	{
		vekt::mono::PluginProcessor processor;
		constexpr int block = 256;
		setParameter(processor, vekt::mono::parameters::trackingOversampling, qualityChoice(factor));
		processor.prepareToPlay(rate, block);
		juce::AudioBuffer<float> buffer(2, block);
		juce::MidiBuffer midi;
		midi.addEvent(juce::MidiMessage::noteOn(1, 45, 0.8f), 0);
		processor.processBlock(buffer, midi);
		midi.clear();
		const auto blocks = static_cast<int>(2.0 * rate / block);
		const auto start = std::chrono::steady_clock::now();
		for (int index = 0; index < blocks; ++index) processor.processBlock(buffer, midi);
		const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		std::cout << "processor default patch A2 " << factor << "x " << std::setw(6)
			<< 100.0 * seconds / (blocks * block / rate) << " % core, latency " << processor.getLatencySamples() << " samples\n";
	}

	// Inharmonic (aliased) output power: osc 1 only, Morph 3, Width 5 %, Octave +2, 44.1 kHz, static
	// note, filter at its defaults or open. Harmonics are located from the measured fundamental.
	for (const auto factor : { 1, 2, 4 })
		for (const auto open : { false, true })
			for (const auto note : { 72, 79, 84 })
			{
				constexpr double labRate = 44'100.0;
				constexpr int block = 512, order = 15, size = 1 << order;
				vekt::mono::PluginProcessor processor;
				for (const auto& [identifier, value] : { std::pair { vekt::mono::parameters::osc1Morph, 3.0f },
					std::pair { vekt::mono::parameters::osc1PulseWidth, 5.0f }, std::pair { vekt::mono::parameters::osc1Octave, 2.0f },
					std::pair { vekt::mono::parameters::osc1Level, 100.0f }, std::pair { vekt::mono::parameters::osc2Level, 0.0f },
					std::pair { vekt::mono::parameters::osc3Level, 0.0f }, std::pair { vekt::mono::parameters::noiseLevel, 0.0f },
					std::pair { vekt::mono::parameters::ampSustain, 100.0f }, std::pair { vekt::mono::parameters::drift, 0.0f },
					std::pair { vekt::mono::parameters::unison, 0.0f }, std::pair { vekt::mono::parameters::trackingOversampling, qualityChoice(factor) } })
					setParameter(processor, identifier, value);
				if (open)
				{
					setParameter(processor, vekt::mono::parameters::filterCutoff, 20'000.0f);
					setParameter(processor, vekt::mono::parameters::filterResonance, 0.0f);
					setParameter(processor, vekt::mono::parameters::filterEnvelopeAmount, 0.0f);
				}
				processor.prepareToPlay(labRate, block);
				juce::AudioBuffer<float> buffer(2, block);
				juce::MidiBuffer midi;
				midi.addEvent(juce::MidiMessage::noteOn(1, note, 0.8f), 0);
				std::vector<float> output;
				for (int index = 0; index < (size + 22'050) / block + 1; ++index)
				{
					buffer.clear();
					processor.processBlock(buffer, midi);
					midi.clear();
					for (int sample = 0; sample < block; ++sample) output.push_back(buffer.getSample(0, sample));
				}
				std::vector<float> frame(2 * size);
				for (int sample = 0; sample < size; ++sample)
					frame[static_cast<std::size_t>(sample)] = output[static_cast<std::size_t>(22'050 + sample)]
						* static_cast<float>(0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * sample / size));
				juce::dsp::FFT(order).performFrequencyOnlyForwardTransform(frame.data());
				// Fundamental: strongest bin within +/-10 % of the nominal pitch, parabolically refined.
				const auto nominal = 440.0 * std::exp2((note - 69) / 12.0) * 4.0;
				auto peak = static_cast<int>(0.9 * nominal * size / labRate);
				for (auto bin = peak; bin < static_cast<int>(1.1 * nominal * size / labRate); ++bin)
					if (frame[static_cast<std::size_t>(bin)] > frame[static_cast<std::size_t>(peak)]) peak = bin;
				const auto a = std::log(frame[static_cast<std::size_t>(peak - 1)] + 1.0e-20f);
				const auto b = std::log(frame[static_cast<std::size_t>(peak)] + 1.0e-20f);
				const auto c = std::log(frame[static_cast<std::size_t>(peak + 1)] + 1.0e-20f);
				const auto pitch = (peak + 0.5 * (a - c) / (a - 2.0f * b + c)) * labRate / size;
				double harmonic {}, inharmonic {};
				for (int bin = 3; bin < size / 2; ++bin)
				{
					const auto frequency = bin * labRate / size;
					const auto nearest = std::round(frequency / pitch);
					const auto power = static_cast<double>(frame[static_cast<std::size_t>(bin)]) * frame[static_cast<std::size_t>(bin)];
					if (nearest >= 1.0 && std::abs(frequency - nearest * pitch) < 6.0 * labRate / size) harmonic += power;
					else inharmonic += power;
				}
				std::cout << "alias 44.1k " << factor << "x filter=" << (open ? "open   " : "default") << " note=" << note
					<< " (" << pitch << " Hz) inharmonic=" << std::setw(7)
					<< 10.0 * std::log10(std::max(inharmonic, 1.0e-30) / harmonic) << " dB\n";
			}
	return 0;
}
