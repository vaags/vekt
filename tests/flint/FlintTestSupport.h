#pragma once

#include "FlintEngine.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <span>
#include <vector>

// Rendering and measurement helpers for Flint's engine tests.
namespace vekt::test::flint
{
using vekt::flint::FlintEngine;
using vekt::flint::FlintParameters;
using vekt::flint::Strike;

// Renders `count` samples of the engine's left channel in blocks of `blockSize`.
inline void renderInto(FlintEngine& engine, std::vector<double>& output, std::size_t count, std::size_t blockSize = 64)
{
	std::vector<float> left(blockSize), right(blockSize);
	for (std::size_t done = 0; done < count; done += blockSize)
	{
		const auto size = std::min(blockSize, count - done);
		const auto leftSpan = std::span(left).first(size), rightSpan = std::span(right).first(size);
		if (engine.isActive())
			engine.process(leftSpan, rightSpan);
		else
		{
			std::fill(leftSpan.begin(), leftSpan.end(), 0.0f);
			std::fill(rightSpan.begin(), rightSpan.end(), 0.0f);
		}
		for (const auto sample : leftSpan) output.push_back(static_cast<double>(sample));
	}
}

// Prepares and activates `engine`, strikes it once and renders `seconds`.
inline std::vector<double> strikeAndRender(FlintEngine& engine, const FlintParameters& parameters, double sampleRate,
    double seconds, Strike strike = { 1.0, 60, 1 })
{
	engine.prepare(sampleRate, 64);
	engine.activate(parameters);
	engine.update(parameters);
	engine.trigger(strike);
	std::vector<double> output;
	renderInto(engine, output, static_cast<std::size_t>(seconds * sampleRate));
	return output;
}

inline double peak(std::span<const double> samples)
{
	auto value = 0.0;
	for (const auto sample : samples) value = std::max(value, std::abs(sample));
	return value;
}

inline double largestStep(std::span<const double> samples)
{
	auto value = 0.0;
	for (std::size_t index = 1; index < samples.size(); ++index)
		value = std::max(value, std::abs(samples[index] - samples[index - 1]));
	return value;
}

inline double rms(std::span<const double> samples)
{
	if (samples.empty()) return 0.0;
	const auto sum =
	    std::accumulate(samples.begin(), samples.end(), 0.0, [](double total, double x) { return total + x * x; });
	return std::sqrt(sum / static_cast<double>(samples.size()));
}

// Frequency from the rising zero crossings in `samples`, linearly interpolated: exact for a decaying sinusoid, whose
// zeros stay periodic.
inline double zeroCrossingFrequency(std::span<const double> samples, double sampleRate)
{
	std::vector<double> crossings;
	for (std::size_t index = 1; index < samples.size(); ++index)
		if (samples[index - 1] < 0.0 && samples[index] >= 0.0)
			crossings.push_back(
			    static_cast<double>(index - 1) + samples[index - 1] / (samples[index - 1] - samples[index]));
	if (crossings.size() < 2) return 0.0;
	return sampleRate * static_cast<double>(crossings.size() - 1) / (crossings.back() - crossings.front());
}

// T60 from the slope of the RMS envelope (10 ms windows) between two levels below its peak, in dB.
inline double t60(
    std::span<const double> samples, double sampleRate, double fromDecibels = -10.0, double toDecibels = -40.0)
{
	const auto window = static_cast<std::size_t>(0.01 * sampleRate);
	std::vector<double> levels;
	for (std::size_t start = 0; start + window <= samples.size(); start += window)
		levels.push_back(20.0 * std::log10(std::max(rms(samples.subspan(start, window)), 1.0e-30)));
	const auto top = *std::max_element(levels.begin(), levels.end());
	auto first = levels.size(), last = levels.size();
	for (std::size_t index = 0; index < levels.size(); ++index)
	{
		if (first == levels.size() && levels[index] <= top + fromDecibels) first = index;
		if (last == levels.size() && levels[index] <= top + toDecibels) last = index;
	}
	if (first >= last || last == levels.size()) return 0.0;
	const auto slope = (levels[last] - levels[first]) / (static_cast<double>(last - first) * 0.01);
	return -60.0 / slope;
}

// Brightness: the mean squared first difference over the mean square. It rises monotonically with the spectral
// centroid's weight at high frequencies (each component contributes 4 sin^2(w / 2) of its power).
inline double brightness(std::span<const double> samples)
{
	auto difference = 0.0, power = 0.0;
	for (std::size_t index = 1; index < samples.size(); ++index)
	{
		difference += (samples[index] - samples[index - 1]) * (samples[index] - samples[index - 1]);
		power += samples[index] * samples[index];
	}
	return power > 0.0 ? difference / power : 0.0;
}

inline double mean(std::span<const double> samples)
{
	return samples.empty() ? 0.0
	                       : std::accumulate(samples.begin(), samples.end(), 0.0) / static_cast<double>(samples.size());
}

inline double decibels(double ratio) { return 20.0 * std::log10(ratio); }
}
