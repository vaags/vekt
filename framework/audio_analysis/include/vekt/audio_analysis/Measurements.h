#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace vekt::audio_analysis
{
struct SampleStatistics
{
	double rms {};
	double peak {};
	double dc {};
	double differenceRms {};
	double crestFactor {};
	std::size_t samples {};
};

class SampleStatisticsAccumulator final
{
public:
	template <typename Sample>
	void add(Sample value) noexcept
	{
		const auto promoted = static_cast<double>(value);
		sumSquares += promoted * promoted;
		sum += promoted;
		peak = std::max(peak, std::abs(promoted));
		if (samples > 0)
		{
			const auto difference = value - static_cast<Sample>(previous);
			const auto promotedDifference = static_cast<double>(difference);
			differenceSquares += promotedDifference * promotedDifference;
		}
		previous = promoted;
		++samples;
	}

	[[nodiscard]] SampleStatistics result() const noexcept
	{
		const auto count = static_cast<double>(std::max<std::size_t>(1, samples));
		const auto rms = std::sqrt(sumSquares / count);
		return {
			rms,
			peak,
			sum / count,
			samples > 1 ? std::sqrt(differenceSquares / static_cast<double>(samples - 1)) : 0.0,
			rms > 0.0 ? peak / rms : 0.0,
			samples
		};
	}

private:
	double sumSquares {};
	double sum {};
	double differenceSquares {};
	double peak {};
	double previous {};
	std::size_t samples {};
};

template <typename Sample>
[[nodiscard]] SampleStatistics measureSamples(std::span<const Sample> samples) noexcept
{
	SampleStatisticsAccumulator accumulator;
	for (const auto sample : samples) accumulator.add(sample);
	return accumulator.result();
}

struct SinusoidalProjection
{
	double real {};
	double imaginary {};

	[[nodiscard]] double magnitude() const noexcept { return std::hypot(real, imaginary); }
	[[nodiscard]] double phaseRadians() const noexcept { return std::atan2(imaginary, real); }
};

class SinusoidalProjector final
{
public:
	SinusoidalProjector(double newSampleRate, double newFrequency,
		std::size_t newStartingSample = 0, double newPhaseMultiplier = 1.0) noexcept
		: sampleRate(newSampleRate), frequency(newFrequency), startingSample(newStartingSample),
		phaseMultiplier(newPhaseMultiplier)
	{
	}

	void add(double sample) noexcept
	{
		const auto absoluteSample = static_cast<double>(startingSample + samples);
		const auto basePhase = 2.0 * std::numbers::pi * frequency * absoluteSample / sampleRate;
		const auto phase = phaseMultiplier * basePhase;
		projection.real += sample * std::cos(phase);
		projection.imaginary -= sample * std::sin(phase);
		++samples;
	}

	[[nodiscard]] SinusoidalProjection result() const noexcept { return projection; }
	[[nodiscard]] double peakAmplitude() const noexcept
	{
		return samples > 0 ? 2.0 * projection.magnitude() / static_cast<double>(samples) : 0.0;
	}

private:
	double sampleRate {};
	double frequency {};
	std::size_t startingSample {};
	double phaseMultiplier { 1.0 };
	SinusoidalProjection projection;
	std::size_t samples {};
};

[[nodiscard]] inline double wrapPhase(double radians) noexcept
{
	return std::remainder(radians, 2.0 * std::numbers::pi);
}

[[nodiscard]] inline double gainToDecibels(double gain, double floor = 1.0e-12) noexcept
{
	return 20.0 * std::log10(std::max(floor, gain));
}

[[nodiscard]] inline double levelMatchScale(double referenceMagnitude, double candidateMagnitude) noexcept
{
	return candidateMagnitude > 0.0 ? referenceMagnitude / candidateMagnitude : 1.0;
}

struct TransferMeasurement
{
	SinusoidalProjection input;
	SinusoidalProjection output;
	double gain {};
	double phaseRadians {};
};

template <typename InputSample, typename OutputSample>
[[nodiscard]] TransferMeasurement measureTransfer(std::span<const InputSample> input,
	std::span<const OutputSample> output, double sampleRate, double frequency,
	std::size_t startingSample = 0) noexcept
{
	SinusoidalProjector inputProjector(sampleRate, frequency, startingSample);
	SinusoidalProjector outputProjector(sampleRate, frequency, startingSample);
	const auto samples = std::min(input.size(), output.size());
	for (std::size_t sample = 0; sample < samples; ++sample)
	{
		inputProjector.add(static_cast<double>(input[sample]));
		outputProjector.add(static_cast<double>(output[sample]));
	}
	const auto inputProjection = inputProjector.result();
	const auto outputProjection = outputProjector.result();
	return {
		inputProjection,
		outputProjection,
		inputProjection.magnitude() > 0.0
			? outputProjection.magnitude() / inputProjection.magnitude() : 0.0,
		wrapPhase(outputProjection.phaseRadians() - inputProjection.phaseRadians())
	};
}

template <std::size_t Harmonics, typename Sample>
[[nodiscard]] std::array<double, Harmonics> measureHarmonicPeakAmplitudes(
	std::span<const Sample> samples, double sampleRate, double fundamentalFrequency,
	std::size_t startingSample = 0) noexcept
{
	std::array<SinusoidalProjector, Harmonics> projectors = [&]<std::size_t... Indices>(
		std::index_sequence<Indices...>)
	{
		return std::array<SinusoidalProjector, Harmonics> {
			SinusoidalProjector(sampleRate, fundamentalFrequency, startingSample,
				static_cast<double>(Indices + 1))...
		};
	}(std::make_index_sequence<Harmonics> {});
	for (const auto sample : samples)
		for (auto& projector : projectors) projector.add(static_cast<double>(sample));
	std::array<double, Harmonics> amplitudes {};
	for (std::size_t harmonic = 0; harmonic < Harmonics; ++harmonic)
		amplitudes[harmonic] = projectors[harmonic].peakAmplitude();
	return amplitudes;
}

struct TwoToneComponents
{
	double firstFundamental {};
	double secondFundamental {};
	double lowerThirdOrder {};
	double upperThirdOrder {};
	double difference {};
	double sum {};
};

template <typename Sample>
[[nodiscard]] TwoToneComponents measureTwoToneComponents(std::span<const Sample> samples,
	double sampleRate, double firstFrequency, double secondFrequency,
	std::size_t startingSample = 0) noexcept
{
	const std::array frequencies {
		firstFrequency,
		secondFrequency,
		std::abs(2.0 * firstFrequency - secondFrequency),
		std::abs(2.0 * secondFrequency - firstFrequency),
		std::abs(secondFrequency - firstFrequency),
		firstFrequency + secondFrequency
	};
	std::array<SinusoidalProjector, frequencies.size()> projectors {
		SinusoidalProjector(sampleRate, frequencies[0], startingSample),
		SinusoidalProjector(sampleRate, frequencies[1], startingSample),
		SinusoidalProjector(sampleRate, frequencies[2], startingSample),
		SinusoidalProjector(sampleRate, frequencies[3], startingSample),
		SinusoidalProjector(sampleRate, frequencies[4], startingSample),
		SinusoidalProjector(sampleRate, frequencies[5], startingSample)
	};
	for (const auto sample : samples)
		for (auto& projector : projectors) projector.add(static_cast<double>(sample));
	return {
		projectors[0].peakAmplitude(),
		projectors[1].peakAmplitude(),
		projectors[2].peakAmplitude(),
		projectors[3].peakAmplitude(),
		projectors[4].peakAmplitude(),
		projectors[5].peakAmplitude()
	};
}

struct ComponentResidual
{
	double rms {};
	double peak {};
};

template <typename Sample>
[[nodiscard]] ComponentResidual measureComponentResidual(std::span<const Sample> samples,
	double sampleRate, std::span<const double> componentFrequencies,
	std::size_t startingSample = 0)
{
	if (samples.empty()) return {};
	std::vector<SinusoidalProjector> projectors;
	projectors.reserve(componentFrequencies.size());
	for (const auto frequency : componentFrequencies)
		projectors.emplace_back(sampleRate, frequency, startingSample);
	for (const auto sample : samples)
		for (auto& projector : projectors) projector.add(static_cast<double>(sample));

	std::vector<SinusoidalProjection> projections;
	projections.reserve(projectors.size());
	for (const auto& projector : projectors) projections.push_back(projector.result());
	const auto scale = 2.0 / static_cast<double>(samples.size());
	SampleStatisticsAccumulator residual;
	for (std::size_t sample = 0; sample < samples.size(); ++sample)
	{
		const auto absoluteSample = static_cast<double>(startingSample + sample);
		double reconstruction {};
		for (std::size_t component = 0; component < componentFrequencies.size(); ++component)
		{
			const auto phase = 2.0 * std::numbers::pi * componentFrequencies[component]
				* absoluteSample / sampleRate;
			reconstruction += scale * (projections[component].real * std::cos(phase)
				- projections[component].imaginary * std::sin(phase));
		}
		residual.add(static_cast<double>(samples[sample]) - reconstruction);
	}
	const auto statistics = residual.result();
	return { statistics.rms, statistics.peak };
}

struct SampleComparison
{
	bool exact {};
	std::size_t comparedSamples {};
	std::size_t firstDifferentSample { std::numeric_limits<std::size_t>::max() };
	double maximumAbsoluteDifference {};
	double rmsDifference {};
};

template <typename FirstSample, typename SecondSample>
[[nodiscard]] SampleComparison compareSamples(std::span<const FirstSample> first,
	std::span<const SecondSample> second) noexcept
{
	SampleComparison result;
	result.comparedSamples = std::min(first.size(), second.size());
	result.exact = first.size() == second.size();
	double differenceSquares {};
	for (std::size_t sample = 0; sample < result.comparedSamples; ++sample)
	{
		const auto difference = static_cast<double>(first[sample]) - static_cast<double>(second[sample]);
		const auto absoluteDifference = std::abs(difference);
		result.maximumAbsoluteDifference = std::max(result.maximumAbsoluteDifference, absoluteDifference);
		differenceSquares += difference * difference;
		bool equal = false;
		if constexpr (std::is_same_v<FirstSample, SecondSample>
			&& std::is_floating_point_v<FirstSample>)
		{
			using Bits = std::conditional_t<sizeof(FirstSample) == sizeof(std::uint32_t),
				std::uint32_t, std::uint64_t>;
			equal = std::bit_cast<Bits>(first[sample]) == std::bit_cast<Bits>(second[sample]);
		}
		else
		{
			equal = first[sample] == second[sample];
		}
		if (!equal && result.firstDifferentSample == std::numeric_limits<std::size_t>::max())
			result.firstDifferentSample = sample;
		result.exact = result.exact && equal;
	}
	if (first.size() != second.size()
		&& result.firstDifferentSample == std::numeric_limits<std::size_t>::max())
		result.firstDifferentSample = result.comparedSamples;
	result.rmsDifference = result.comparedSamples > 0
		? std::sqrt(differenceSquares / static_cast<double>(result.comparedSamples)) : 0.0;
	return result;
}

struct RingdownMeasurement
{
	double peak {};
	std::size_t onsetSample {};
	std::size_t peakSample {};
	std::size_t extinctionSample {};
	bool extinguished {};
	double estimatedFrequencyHz {};
};

template <typename Sample>
[[nodiscard]] RingdownMeasurement measureRingdown(std::span<const Sample> samples,
	double sampleRate, double onsetRatio = 0.1, double extinctionRatio = 0.001,
	double minimumExtinctionThreshold = 1.0e-7, double extinctionHoldSeconds = 0.01,
	double frequencyWindowSeconds = 0.5) noexcept
{
	RingdownMeasurement result;
	result.extinctionSample = samples.size();
	if (samples.empty() || sampleRate <= 0.0) return result;

	for (std::size_t sample = 0; sample < samples.size(); ++sample)
		if (const auto magnitude = std::abs(static_cast<double>(samples[sample])); magnitude > result.peak)
		{
			result.peak = magnitude;
			result.peakSample = sample;
		}

	const auto onsetThreshold = result.peak * onsetRatio;
	while (result.onsetSample < result.peakSample
		&& std::abs(static_cast<double>(samples[result.onsetSample])) < onsetThreshold)
		++result.onsetSample;

	const auto extinctionThreshold = std::max(minimumExtinctionThreshold, result.peak * extinctionRatio);
	const auto holdSamples = std::max<std::size_t>(1,
		static_cast<std::size_t>(std::llround(sampleRate * extinctionHoldSeconds)));
	for (auto sample = result.peakSample; sample + holdSamples < samples.size(); ++sample)
	{
		bool below = true;
		for (std::size_t offset = 0; offset < holdSamples; ++offset)
			below = below && std::abs(static_cast<double>(samples[sample + offset])) <= extinctionThreshold;
		if (below)
		{
			result.extinctionSample = sample;
			result.extinguished = true;
			break;
		}
	}

	const auto start = std::max<std::size_t>(1, result.onsetSample);
	const auto windowSamples = static_cast<std::size_t>(std::llround(sampleRate * frequencyWindowSeconds));
	const auto end = std::min(samples.size(), start + windowSamples);
	std::size_t crossings {}, firstCrossing {}, lastCrossing {};
	for (auto sample = start; sample < end; ++sample)
		if (samples[sample - 1] <= Sample {} && samples[sample] > Sample {})
		{
			if (crossings == 0) firstCrossing = sample;
			lastCrossing = sample;
			++crossings;
		}
	if (crossings > 1 && lastCrossing > firstCrossing)
		result.estimatedFrequencyHz = static_cast<double>(crossings - 1) * sampleRate
			/ static_cast<double>(lastCrossing - firstCrossing);
	return result;
}

template <typename Sample>
[[nodiscard]] bool samplesExactlyEqual(std::span<const Sample> first,
	std::span<const Sample> second) noexcept
{
	return compareSamples(first, second).exact;
}
}