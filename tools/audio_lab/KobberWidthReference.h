#pragma once

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <map>
#include <numbers>
#include <vector>

namespace vekt::audio_lab
{
// Offline-only periodic Fourier reference. The source is the *ideal* shared phase
// warp, not the production oscillator: no production wavetable data.
// Fourier coefficients are obtained from a dense, midpoint-sampled cycle and
// only harmonics strictly below the destination Nyquist are synthesized.
// The FFT uses float, so this is a finite-precision static reference, not an
// exact continuous-time integral or a reference for moving Width.
struct KobberWidthReference
{
	std::vector<std::complex<double>> harmonics; // c[n], including c[0] (DC)
	std::vector<double> samples;
	double rms {};
};

inline double kobberWidthIdealAnchor(int anchor, double phase, double width)
{
	constexpr double sineGain = 0.81649658;
	constexpr double pulseGain = 0.57735027;
	const auto d = std::clamp(width * 0.01, 0.05, 0.95);
	const auto warped = phase < d ? phase / (2.0 * d) : 0.5 + (phase - d) / (2.0 * (1.0 - d));
	switch (anchor)
	{
	case 0: return sineGain * std::sin(2.0 * std::numbers::pi * warped);
	case 1:
	{
		const auto trianglePhase = warped + 0.25 - std::floor(warped + 0.25);
		return 1.0 - 4.0 * std::abs(trianglePhase - 0.5);
	}
	case 2: return 1.0 - 2.0 * warped;
	default: return pulseGain * (warped < 0.5 ? 1.0 : -1.0);
	}
}

// Cyclic Morph (period 4): the segment, the fraction through it and the second anchor's weight. Sine to
// triangle is linear; elsewhere the richer anchor's share is 2t^2 - t^3 (the saw next to it, the square back to
// the sine).
struct KobberWidthMorphBlend
{
	int from {}, to {};
	double weight {};
};

inline KobberWidthMorphBlend kobberWidthMorphBlend(double morph)
{
	const auto position = morph - 4.0 * std::floor(morph / 4.0);
	const auto segment = std::clamp(static_cast<int>(position), 0, 3);
	const auto fraction = position - segment;
	const auto delayed = [](double t) { return t * t * (2.0 - t); };
	const auto weight = segment == 0 ? fraction : segment == 1 ? delayed(fraction) : 1.0 - delayed(1.0 - fraction);
	return { segment, (segment + 1) % 4, weight };
}

inline double kobberWidthIdealWave(double phase, double morph, double width, bool zeroCentered = false)
{
	const auto blend = kobberWidthMorphBlend(morph);
	const auto anchor = [&](int index)
	{
		auto value = kobberWidthIdealAnchor(index, phase, width);
		if (zeroCentered)
		{
			const auto meanAtUnitAsymmetry = index == 0 ? 2.0 * 0.81649658 / std::numbers::pi
				: index == 3 ? 0.57735027 : 0.5;
			value -= (2.0 * std::clamp(width * 0.01, 0.05, 0.95) - 1.0) * meanAtUnitAsymmetry;
		}
		return value;
	};
	const auto from = anchor(blend.from);
	return from + blend.weight * (anchor(blend.to) - from);
}

// Offline bandwidth policy, separate from the unmodified Fourier correctness
// oracle. Smoothly removes each partial before it reaches the host guard.
inline double kobberWidthGuardGain(int harmonic, double pitch, double sampleRate,
	double start = 0.8, double end = 0.9)
{
	const auto ratio = 2.0 * harmonic * pitch / sampleRate;
	const auto t = std::clamp((ratio - start) / (end - start), 0.0, 1.0);
	return 1.0 - t * t * (3.0 - 2.0 * t);
}

inline std::vector<std::complex<double>> kobberWidthGuardedHarmonics(
	std::vector<std::complex<double>> coefficients, double pitch, double sampleRate,
	double start = 0.8, double end = 0.9)
{
	for (std::size_t harmonic = 1; harmonic < coefficients.size(); ++harmonic)
		coefficients[harmonic] *= kobberWidthGuardGain(static_cast<int>(harmonic), pitch, sampleRate, start, end);
	return coefficients;
}

// resolutionOrder controls quadrature accuracy; frequency must be positive and
// below Nyquist. The output starts at phase zero; arbitrary initial phase can be
// obtained by rotating the Fourier terms during synthesis.
inline KobberWidthReference renderMonoWidthReference(double morph, double width, double frequency,
	double sampleRate, int outputSamples, int resolutionOrder = 16, bool zeroCentered = false)
{
	const auto size = 1 << resolutionOrder;
	std::vector<juce::dsp::Complex<float>> values(static_cast<std::size_t>(size)), transform(static_cast<std::size_t>(size));
	for (int index = 0; index < size; ++index)
		values[static_cast<std::size_t>(index)] = { static_cast<float>(kobberWidthIdealWave((index + 0.5) / size, morph, width, zeroCentered)), 0.0f };
	// Planning a 2^16-point FFT costs more than performing it, and table builds render hundreds of references.
	thread_local std::map<int, juce::dsp::FFT> plans;
	plans.try_emplace(resolutionOrder, resolutionOrder).first->second.perform(values.data(), transform.data(), false);
	KobberWidthReference result;
	const auto maximumHarmonic = std::min(size / 2 - 1,
		static_cast<int>(std::ceil(sampleRate / (2.0 * frequency))) - 1);
	result.harmonics.reserve(static_cast<std::size_t>(maximumHarmonic + 1));
	const auto scale = 1.0 / size;
	for (int harmonic = 0; harmonic <= maximumHarmonic; ++harmonic)
	{
		const auto coefficient = transform[static_cast<std::size_t>(harmonic)];
		// Midpoint quadrature samples represent phase (index + 1/2)/size.
		const auto correction = std::polar(1.0, -std::numbers::pi * harmonic / size);
		result.harmonics.emplace_back(std::complex<double>(coefficient.real(), coefficient.imag()) * scale * correction);
	}
	result.samples.reserve(static_cast<std::size_t>(outputSamples));
	for (int sample = 0; sample < outputSamples; ++sample)
	{
		const auto phase = std::fmod(sample * frequency / sampleRate, 1.0);
		auto value = result.harmonics.front().real();
		for (std::size_t harmonic = 1; harmonic < result.harmonics.size(); ++harmonic)
			value += 2.0 * (result.harmonics[harmonic] * std::polar(1.0, 2.0 * std::numbers::pi * static_cast<double>(harmonic) * phase)).real();
		result.samples.push_back(value);
		result.rms += value * value;
	}
	if (outputSamples > 0) result.rms = std::sqrt(result.rms / outputSamples);
	return result;
}

// Shipped Width model (chosen 29 Sep 2026), derived independently of KobberVoice
// from the ideal shapes above. Sine, triangle and square are the shared warp at
// breakpoint 50 + depth * (width - 50) with depths 0.45/0.65/1.0, rotated so the
// fundamental is +sin (exact; production interpolates a 0.5% alignment table).
// The saw is a two-tooth saw: the mean of two neutral saws offset by +/- delta/2,
// delta = 0.25 * |width - 50| / 45, i.e. c[h] * cos(pi h delta), normalized to
// the saw's RMS by 1 / sqrt(1 - 3 delta + 3 delta^2). Returns c[0..size/2-1].
inline std::vector<std::complex<double>> kobberWidthAnchorCoefficients(int anchor, double width,
	int resolutionOrder = 16)
{
	constexpr std::array depths { 0.45, 0.65, 0.0, 1.0 };
	const auto size = static_cast<double>(1 << resolutionOrder);
	const auto offset = std::clamp(width, 5.0, 95.0) - 50.0;
	if (anchor == 2 || offset == 0.0)
	{
		auto harmonics = renderMonoWidthReference(anchor, 50.0, 1.0, size, 0, resolutionOrder).harmonics;
		if (anchor != 2 || offset == 0.0) return harmonics;
		const auto delta = 0.25 * std::abs(offset) / 45.0;
		const auto gain = 1.0 / std::sqrt(1.0 - 3.0 * delta + 3.0 * delta * delta);
		for (std::size_t harmonic = 1; harmonic < harmonics.size(); ++harmonic)
			harmonics[harmonic] *= gain * std::cos(std::numbers::pi * static_cast<double>(harmonic) * delta);
		return harmonics;
	}
	auto harmonics = renderMonoWidthReference(anchor, 50.0 + depths[static_cast<std::size_t>(anchor)] * offset,
		1.0, size, 0, resolutionOrder).harmonics;
	const auto shift = -0.5 * std::numbers::pi - std::arg(harmonics[1]);
	for (std::size_t harmonic = 1; harmonic < harmonics.size(); ++harmonic)
		harmonics[harmonic] *= std::polar(1.0, shift * static_cast<double>(harmonic));
	return harmonics;
}

// Morph blend of the shipped anchors, same weights as kobberWidthIdealWave.
inline std::vector<std::complex<double>> kobberWidthShippedCoefficients(double morph, double width,
	int resolutionOrder = 16)
{
	const auto blend = kobberWidthMorphBlend(morph);
	auto from = kobberWidthAnchorCoefficients(blend.from, width, resolutionOrder);
	if (blend.weight <= 0.0) return from;
	const auto to = kobberWidthAnchorCoefficients(blend.to, width, resolutionOrder);
	for (std::size_t index = 0; index < from.size(); ++index) from[index] += blend.weight * (to[index] - from[index]);
	return from;
}
}
