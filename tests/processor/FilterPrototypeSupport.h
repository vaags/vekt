#pragma once

// Shared helpers for the Kobber filter-prototype characterizations (the K35 candidate, ADR 0007):
// parallel jobs, a band-limited saw, cycle and harmonic analysis, and WAV output.

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <numbers>
#include <string>
#include <thread>
#include <vector>

namespace vekt::test::filter_prototype
{
// Runs job(0) .. job(count - 1) on every hardware thread and returns the results in order, for the long hidden
// characterizations. Jobs must not use Catch2 assertions (not thread-safe): tests assert on the results afterwards.
template <typename Job>
auto parallelMap(std::size_t count, Job job)
{
	std::vector<decltype(job(std::size_t {}))> results(count);
	std::atomic<std::size_t> next {};
	std::vector<std::thread> workers;
	for (unsigned worker = 0; worker < std::max(1u, std::thread::hardware_concurrency()); ++worker)
		workers.emplace_back([&]
		{
			for (auto index = next++; index < count; index = next++) results[index] = job(index);
		});
	for (auto& worker : workers) worker.join();
	return results;
}

// Amplitude of the component at a frequency after settling, by projection over whole periods of it.
inline double amplitudeAt(const std::vector<double>& y, double frequency, double sampleRate, int periodSamples)
{
	const auto usable = y.size() / static_cast<std::size_t>(periodSamples) * static_cast<std::size_t>(periodSamples);
	double inPhase {}, quadrature {};
	for (std::size_t sample = 0; sample < usable; ++sample)
	{
		const auto phase = 2.0 * std::numbers::pi * frequency * static_cast<double>(sample) / sampleRate;
		inPhase += y[sample] * std::sin(phase);
		quadrature += y[sample] * std::cos(phase);
	}
	return 2.0 * std::hypot(inPhase, quadrature) / static_cast<double>(usable);
}

struct CycleWindow
{
	double frequency {}, peak {}, rms {}, fundamental {}, thd {}, evenShare {};
};

// Frequency from interpolated positive zero crossings; harmonic levels by Blackman-Harris-windowed projection at
// multiples of that frequency (a rectangular window over a non-integer cycle count leaks about 1e-3 into every
// harmonic, which would hide the even-harmonic floor).
inline CycleWindow analyseCycles(const std::vector<double>& y, double sampleRate)
{
	std::vector<double> crossings;
	for (std::size_t sample = 1; sample < y.size(); ++sample)
		if (y[sample - 1] < 0.0 && y[sample] >= 0.0)
			crossings.push_back(static_cast<double>(sample - 1) + y[sample - 1] / (y[sample - 1] - y[sample]));
	CycleWindow result;
	if (crossings.size() < 3) return result;
	const auto cycles = static_cast<double>(crossings.size() - 1);
	result.frequency = cycles * sampleRate / (crossings.back() - crossings.front());
	const auto first = static_cast<std::size_t>(std::ceil(crossings.front()));
	const auto last = static_cast<std::size_t>(std::floor(crossings.back()));
	const auto count = static_cast<double>(last - first + 1);
	double energy {}, weightSum {};
	std::array<double, 21> level {}, inPhase {}, quadrature {};
	for (auto sample = first; sample <= last; ++sample)
	{
		const auto value = y[sample];
		result.peak = std::max(result.peak, std::abs(value));
		energy += value * value;
		const auto x = 2.0 * std::numbers::pi * (static_cast<double>(sample - first) + 0.5) / count;
		const auto weight = 0.35875 - 0.48829 * std::cos(x) + 0.14128 * std::cos(2.0 * x) - 0.01168 * std::cos(3.0 * x);
		weightSum += weight;
		const auto phase = 2.0 * std::numbers::pi * result.frequency * static_cast<double>(sample) / sampleRate;
		for (std::size_t harmonic = 1; harmonic < level.size(); ++harmonic)
		{
			inPhase[harmonic] += weight * value * std::sin(static_cast<double>(harmonic) * phase);
			quadrature[harmonic] += weight * value * std::cos(static_cast<double>(harmonic) * phase);
		}
	}
	result.rms = std::sqrt(energy / count);
	double harmonics {}, even {};
	for (std::size_t harmonic = 1; harmonic < level.size(); ++harmonic)
	{
		if (static_cast<double>(harmonic) * result.frequency >= 0.5 * sampleRate) break;
		level[harmonic] = 2.0 * std::hypot(inPhase[harmonic], quadrature[harmonic]) / weightSum;
		if (harmonic == 1) continue;
		harmonics += level[harmonic] * level[harmonic];
		if (harmonic % 2 == 0) even += level[harmonic] * level[harmonic];
	}
	result.fundamental = level[1];
	result.thd = std::sqrt(harmonics) / level[1];
	result.evenShare = std::sqrt(even) / level[1];
	return result;
}

inline std::string dB(double gain) { return gain <= 0.0 ? std::string("-inf") : juce::String(20.0 * std::log10(gain), 1).toStdString(); }

// A band-limited saw of unit peak amplitude (additive, harmonics below 20 kHz), phase-continuous.
struct AdditiveSaw
{
	double frequency {}, sampleRate {}, phase {};
	double next()
	{
		// sin(k phase) by rotating e^(i phase) k times: one sin/cos per sample, not one per harmonic.
		const auto stepCos = std::cos(phase), stepSin = std::sin(phase);
		double value {}, harmonicCos = 1.0, harmonicSin = 0.0;
		for (int harmonic = 1; harmonic * frequency < 20'000.0; ++harmonic)
		{
			const auto nextCos = harmonicCos * stepCos - harmonicSin * stepSin;
			harmonicSin = harmonicSin * stepCos + harmonicCos * stepSin;
			harmonicCos = nextCos;
			value += (harmonic % 2 == 1 ? 1.0 : -1.0) * harmonicSin / harmonic;
		}
		phase += 2.0 * std::numbers::pi * frequency / sampleRate;
		if (phase > 2.0 * std::numbers::pi) phase -= 2.0 * std::numbers::pi;
		return 2.0 / std::numbers::pi * value;
	}
};

inline double rmsOf(const std::vector<float>& samples)
{
	double energy {};
	for (const auto sample : samples) energy += static_cast<double>(sample) * sample;
	return std::sqrt(energy / static_cast<double>(samples.size()));
}

inline void writeWav(const juce::File& file, const std::vector<float>& samples, double gain, double sampleRate)
{
	juce::AudioBuffer<float> buffer(1, static_cast<int>(samples.size()));
	buffer.copyFrom(0, 0, samples.data(), static_cast<int>(samples.size()), static_cast<float>(gain));
	file.deleteFile();
	std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
	juce::WavAudioFormat format;
	auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions {}.withSampleRate(sampleRate).withNumChannels(1).withBitsPerSample(32));
	REQUIRE(writer != nullptr);
	REQUIRE(writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples()));
}
}
