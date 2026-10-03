#pragma once

#include "KobberWidthReference.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <map>
#include <vector>

namespace vekt::audio_lab
{
// Offline-only greedy Width experiment. Its objective is complex harmonic
// error, not alias power. Training and validation Width sets must be disjoint.
class KobberWidthAdaptiveKnots
{
public:
	using Spectrum = std::vector<std::complex<double>>;
	using Anchors = std::array<Spectrum, 4>;
	struct Worst { double ratio {}; double width {}; int bin {}; double morph {}; };

	explicit KobberWidthAdaptiveKnots(std::vector<double> trainingWidths) : training(std::move(trainingWidths))
	{
		for (const auto width : this->training) (void)at(width);
	}

	const Anchors& at(double width) const
	{
		if (const auto found = cache.find(width); found != cache.end()) return found->second;
		Anchors result;
		for (int anchor = 0; anchor < 4; ++anchor)
			result[static_cast<std::size_t>(anchor)] = renderMonoWidthReference(anchor, width, 1.0, 65536.0, 0).harmonics;
		return cache.emplace(width, std::move(result)).first->second;
	}

	static Spectrum interpolate(const std::vector<double>& knots, double width, int anchor,
		int harmonicLimit, const KobberWidthAdaptiveKnots& source)
	{
		const auto upper = std::lower_bound(knots.begin(), knots.end(), width);
		const auto high = static_cast<std::size_t>(upper - knots.begin());
		const auto low = high == 0 ? high : high - 1;
		const auto first = std::min(low == 0 ? std::size_t { 0 } : low - 1, knots.size() - 4);
		Spectrum result(static_cast<std::size_t>(harmonicLimit + 1));
		for (std::size_t index = first; index < first + 4; ++index)
		{
			double weight = 1.0;
			for (std::size_t other = first; other < first + 4; ++other)
				if (index != other) weight *= (width - knots[other]) / (knots[index] - knots[other]);
			const auto& frame = source.at(knots[index])[static_cast<std::size_t>(anchor)];
			for (int harmonic = 0; harmonic <= harmonicLimit; ++harmonic)
				result[static_cast<std::size_t>(harmonic)] += weight * frame[static_cast<std::size_t>(harmonic)];
		}
		return result;
	}

	Worst evaluate(const std::vector<double>& knots, const std::vector<double>& widths) const
	{
		Worst worst;
		for (const auto width : widths)
		{
			const auto& ideal = at(width);
			for (const auto [bin, limit] : { std::pair { 37, 27 }, std::pair { 151, 6 } })
			{
				std::array<Spectrum, 4> candidates;
				for (int anchor = 0; anchor < 4; ++anchor)
					candidates[static_cast<std::size_t>(anchor)] = interpolate(knots, width, anchor, limit, *this);
				for (const auto morph : { 0.0, 1.0, 1.5, 2.0, 2.5, 3.0 })
				{
					const auto segment = std::clamp(static_cast<int>(morph), 0, 2);
					const auto fraction = morph - segment;
					const auto sawWeight = [](double t) { return t * t * (2.0 - t); };
					const auto mix = segment == 0 ? fraction : segment == 1 ? sawWeight(fraction) : 1.0 - sawWeight(1.0 - fraction);
					double error {}, power {};
					for (int harmonic = 1; harmonic <= limit; ++harmonic)
					{
						const auto n = static_cast<std::size_t>(harmonic);
						const auto expected = ideal[static_cast<std::size_t>(segment)][n]
							+ mix * (ideal[static_cast<std::size_t>(segment + 1)][n] - ideal[static_cast<std::size_t>(segment)][n]);
						const auto actual = candidates[static_cast<std::size_t>(segment)][n]
							+ mix * (candidates[static_cast<std::size_t>(segment + 1)][n] - candidates[static_cast<std::size_t>(segment)][n]);
						error += 2.0 * std::norm(actual - expected);
						power += 2.0 * std::norm(expected);
					}
					const auto ratio = error / std::max(power, 1.0e-30);
					if (ratio > worst.ratio) worst = { ratio, width, bin, morph };
				}
			}
		}
		return worst;
	}

	std::vector<double> grow(std::vector<double> knots, int count) const
	{
		while (static_cast<int>(knots.size()) < count)
		{
			std::vector<double> eligible;
			for (const auto width : training)
				if (!std::binary_search(knots.begin(), knots.end(), width)) eligible.push_back(width);
			const auto worst = evaluate(knots, eligible);
			knots.insert(std::lower_bound(knots.begin(), knots.end(), worst.width), worst.width);
		}
		return knots;
	}

private:
	std::vector<double> training;
	mutable std::map<double, Anchors> cache;
};
}