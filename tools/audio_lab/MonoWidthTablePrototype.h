#pragma once

#include "MonoWidthReference.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

namespace vekt::audio_lab
{
// Offline static architecture experiment. No allocation occurs during lookup;
// construction, memory layout and pitch-level changes are NOT production-ready.
class MonoWidthTablePrototype
{
public:
	enum class PhaseInterpolation { linear, cubic };
	enum class WidthSpacing { uniform, logit };
	enum class WidthInterpolation { linear, cubic, lagrange6, lagrange8 };

	static std::vector<double> widths(int count, WidthSpacing spacing)
	{
		std::vector<double> result;
		result.reserve(static_cast<std::size_t>(count));
		for (int index = 0; index < count; ++index)
		{
			const auto t = static_cast<double>(index) / (count - 1);
			if (spacing == WidthSpacing::uniform) result.push_back(5.0 + 90.0 * t);
			else
			{
				const auto endpoint = std::log(0.95 / 0.05);
				const auto u = (2.0 * t - 1.0) * endpoint;
				result.push_back(100.0 / (1.0 + std::exp(-u)));
			}
		}
		return result;
	}

	// Levels are indexed by increasing maximum pitch in Hz. Optional fades
	// finish at the brighter level's ceiling; the default retains hard switching.
	MonoWidthTablePrototype(std::vector<double> widthFrames, int order, double internalRate,
		double hostRate, std::vector<double> ceilings, bool useMirrorUpper = false,
		double guard = 1.0, double fadeStart = 1.0,
		std::vector<int> explicitLimits = {}, bool smoothFade = false)
		: frames(std::move(widthFrames)), pitchCeilings(std::move(ceilings)), size(1 << order),
			mirrorUpper(useMirrorUpper), spectralGuard(guard), fadeFraction(fadeStart),
			bandLimits(std::move(explicitLimits)), smoothTransition(smoothFade), outputRate(hostRate)
	{
		// Explicit limits are for the offline harmonic-density experiment;
		// ordinary calls still derive limits from their band ceilings.
		jassert(bandLimits.empty() || bandLimits.size() == pitchCeilings.size());
		jassert(bandLimits.empty() || (fadeFraction < 1.0 && smoothTransition && bandLimits.back() == 0));
		virtualFrames = frames;
		if (mirrorUpper)
			for (std::size_t index = frames.size() - 1; index-- > 0;)
				virtualFrames.push_back(100.0 - frames[index]);
		juce::dsp::FFT inverse(order);
		for (int anchor = 0; anchor < 4; ++anchor)
		{
			tables[static_cast<std::size_t>(anchor)].resize(this->pitchCeilings.size());
			for (auto& level : tables[static_cast<std::size_t>(anchor)])
				level.resize(this->frames.size());
			for (std::size_t frame = 0; frame < this->frames.size(); ++frame)
			{
				// Obtain full source coefficients independently of the runtime table
				// resolution; truncate the identical complex series for every level.
				const auto source = renderMonoWidthReference(anchor, this->frames[frame],
					1.0, 65536.0, 0).harmonics;
				for (std::size_t band = 0; band < this->pitchCeilings.size(); ++band)
				{
					const auto ceiling = this->pitchCeilings[band];
					const auto limit = bandLimits.empty() ? std::min({ (size / 2) - 1,
						static_cast<int>(source.size()) - 1,
						static_cast<int>(std::ceil(spectralGuard * internalRate / (2.0 * ceiling))) - 1,
						static_cast<int>(std::ceil(spectralGuard * hostRate / (2.0 * ceiling))) - 1 })
						: std::min({ bandLimits[band], (size / 2) - 1, static_cast<int>(source.size()) - 1 });
					std::vector<juce::dsp::Complex<float>> bins(static_cast<std::size_t>(size));
					std::vector<juce::dsp::Complex<float>> cycle(static_cast<std::size_t>(size));
					bins[0] = { static_cast<float>(source[0].real()), 0.0f };
					for (int harmonic = 1; harmonic <= limit; ++harmonic)
					{
						const auto coefficient = source[static_cast<std::size_t>(harmonic)];
						bins[static_cast<std::size_t>(harmonic)] = { static_cast<float>(coefficient.real()), static_cast<float>(coefficient.imag()) };
						bins[static_cast<std::size_t>(size - harmonic)] = { static_cast<float>(coefficient.real()), static_cast<float>(-coefficient.imag()) };
					}
					inverse.perform(bins.data(), cycle.data(), true);
					auto& target = tables[static_cast<std::size_t>(anchor)][band][frame];
					target.reserve(static_cast<std::size_t>(size));
					// JUCE inverse FFT normalises by N: bins contain c[n], so
					// multiply output by N to recover the Fourier synthesis value.
					for (const auto& sample : cycle) target.push_back(sample.real() * static_cast<float>(size));
				}
			}
		}
	}

	int harmonicLimit(double pitch, double internalRate, double hostRate) const
	{
		const auto band = bandFor(pitch);
		if (!bandLimits.empty()) return bandLimits[band];
		const auto ceiling = pitchCeilings[band];
		return std::min({ size / 2 - 1,
			static_cast<int>(std::ceil(spectralGuard * internalRate / (2.0 * ceiling))) - 1,
			static_cast<int>(std::ceil(spectralGuard * hostRate / (2.0 * ceiling))) - 1 });
	}

	double wave(double phase, double morph, double width, double pitch, PhaseInterpolation interpolation,
		WidthInterpolation widthInterpolation = WidthInterpolation::linear) const
	{
		const auto band = bandFor(pitch);
		const auto clamped = std::clamp(width, virtualFrames.front(), virtualFrames.back());
		const auto upper = std::lower_bound(virtualFrames.begin(), virtualFrames.end(), clamped);
		const auto high = static_cast<std::size_t>(upper - virtualFrames.begin());
		const auto low = high == 0 ? high : high - 1;
		const auto widthMix = high == low ? 0.0 : (clamped - virtualFrames[low]) / (virtualFrames[high] - virtualFrames[low]);
		const auto segment = std::clamp(static_cast<int>(morph), 0, 2);
		const auto fraction = morph - segment;
		const auto sawWeight = [](double t) { return t * t * (2.0 - t); };
		const auto morphMix = segment == 0 ? fraction : segment == 1 ? sawWeight(fraction) : 1.0 - sawWeight(1.0 - fraction);
		const auto shape = [&](int anchor, std::size_t level)
		{
			const auto& slices = tables[static_cast<std::size_t>(anchor)][level];
			const auto sampleFrame = [&](std::size_t index)
			{
				if (index < frames.size()) return lookup(slices[index], phase, interpolation);
				return -lookup(slices[virtualFrames.size() - 1 - index], 1.0 - phase, interpolation);
			};
			const auto from = sampleFrame(low);
			const auto taps = widthInterpolation == WidthInterpolation::linear ? std::size_t { 2 }
				: widthInterpolation == WidthInterpolation::cubic ? std::size_t { 4 }
				: widthInterpolation == WidthInterpolation::lagrange6 ? std::size_t { 6 } : std::size_t { 8 };
			if (taps > 2 && virtualFrames.size() >= taps && low != high)
			{
				const auto first = std::min(low < (taps - 2) / 2 ? std::size_t { 0 }
					: low - (taps - 2) / 2, virtualFrames.size() - taps);
				double value {};
				for (std::size_t node = first; node < first + taps; ++node)
				{
					double weight = 1.0;
					for (std::size_t other = first; other < first + taps; ++other)
						if (other != node) weight *= (clamped - virtualFrames[other]) / (virtualFrames[node] - virtualFrames[other]);
					value += weight * sampleFrame(node);
				}
				return value;
			}
			return from + widthMix * (sampleFrame(high) - from);
		};
		const auto atBand = [&](std::size_t level)
		{
			const auto from = shape(segment, level);
			return from + morphMix * (shape(segment + 1, level) - from);
		};
		if (!bandLimits.empty())
		{
			// Multiple partials can simultaneously enter the guard. Telescoping
			// adjacent level differences avoids the one-crossfade-at-a-time error.
			// Start from the first fully retained level, and visit only partially
			// active higher levels; zero-gain levels need no phase lookups.
			std::size_t base = bandLimits.size() - 1;
			for (std::size_t level = 0; level + 1 < bandLimits.size(); ++level)
				if (monoWidthGuardGain(bandLimits[level], pitch, outputRate,
					spectralGuard * fadeFraction, spectralGuard) >= 1.0)
				{ base = level; break; }
			auto result = atBand(base);
			for (std::size_t level = 0; level < base; ++level)
			{
				const auto gain = monoWidthGuardGain(bandLimits[level], pitch, outputRate,
					spectralGuard * fadeFraction, spectralGuard);
				if (gain > 0.0) result += gain * (atBand(level) - atBand(level + 1));
			}
			return result;
		}
		const auto brighter = atBand(band);
		if (fadeFraction >= 1.0 || band + 1 == pitchCeilings.size()) return brighter;
		const auto ceiling = pitchCeilings[band];
		const auto start = fadeFraction * ceiling;
		if (pitch <= start) return brighter;
		auto mix = std::clamp((pitch - start) / (ceiling - start), 0.0, 1.0);
		if (smoothTransition) mix = mix * mix * (3.0 - 2.0 * mix);
		return brighter + mix * (atBand(band + 1) - brighter);
	}

	std::size_t bytes() const noexcept
	{
		return 4 * pitchCeilings.size() * frames.size() * static_cast<std::size_t>(size) * sizeof(float);
	}

private:
	std::size_t bandFor(double pitch) const
	{
		const auto it = std::lower_bound(pitchCeilings.begin(), pitchCeilings.end(), pitch);
		return it == pitchCeilings.end() ? pitchCeilings.size() - 1 : static_cast<std::size_t>(it - pitchCeilings.begin());
	}

	double lookup(const std::vector<float>& table, double phase, PhaseInterpolation interpolation) const
	{
		const auto position = (phase - std::floor(phase)) * size;
		const auto index = static_cast<int>(position);
		const auto t = position - index;
		const auto at = [&](int offset) { return static_cast<double>(table[static_cast<std::size_t>((index + offset + size) % size)]); };
		const auto a = at(0), b = at(1);
		if (interpolation == PhaseInterpolation::linear) return a + t * (b - a);
		// Four-point Lagrange interpolation through samples -1, 0, 1, 2.
		return at(-1) * (-t * (t - 1.0) * (t - 2.0) / 6.0)
			+ a * ((t + 1.0) * (t - 1.0) * (t - 2.0) / 2.0)
			+ b * (-(t + 1.0) * t * (t - 2.0) / 2.0)
			+ at(2) * ((t + 1.0) * t * (t - 1.0) / 6.0);
	}

	std::vector<double> frames, pitchCeilings, virtualFrames;
	int size;
	bool mirrorUpper;
	double spectralGuard, fadeFraction;
	std::vector<int> bandLimits;
	bool smoothTransition;
	double outputRate;
	std::array<std::vector<std::vector<std::vector<float>>>, 4> tables;
};
}