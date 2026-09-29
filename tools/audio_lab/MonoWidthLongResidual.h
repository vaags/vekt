#pragma once

#include "MonoWidthReference.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace vekt::audio_lab
{
// Offline, zero-phase experiment only. 'radius' is support on EACH side in
// internal samples. No causal delay, audio-thread allocation, or production API.
class MonoWidthLongResidual
{
public:
	explicit MonoWidthLongResidual(int supportRadius, int tableResolution = 256, double cutoff = 1.0)
		: radius(supportRadius), resolution(tableResolution)
	{
		const auto count = radius * resolution;
		std::vector<double> impulse(static_cast<std::size_t>(count + 1));
		for (int index = 0; index <= count; ++index)
		{
			const auto t = static_cast<double>(index) / resolution;
			const auto window = 0.42 + 0.5 * std::cos(std::numbers::pi * t / radius)
				+ 0.08 * std::cos(2.0 * std::numbers::pi * t / radius);
			impulse[static_cast<std::size_t>(index)] = (index == 0 ? cutoff : std::sin(std::numbers::pi * cutoff * t) / (std::numbers::pi * t)) * window;
		}
		double area = impulse[0] / (2.0 * resolution);
		for (int index = 1; index < count; ++index) area += impulse[static_cast<std::size_t>(index)] / resolution;
		area += impulse[static_cast<std::size_t>(count)] / (2.0 * resolution);
		for (auto& value : impulse) value /= 2.0 * area;
		stepTail.resize(static_cast<std::size_t>(count + 1));
		rampTail.resize(static_cast<std::size_t>(count + 1));
		for (int index = count - 1; index >= 0; --index)
			stepTail[static_cast<std::size_t>(index)] = stepTail[static_cast<std::size_t>(index + 1)]
				+ (impulse[static_cast<std::size_t>(index)] + impulse[static_cast<std::size_t>(index + 1)]) / (2.0 * resolution);
		for (int index = count - 1; index >= 0; --index)
			rampTail[static_cast<std::size_t>(index)] = rampTail[static_cast<std::size_t>(index + 1)]
				+ (stepTail[static_cast<std::size_t>(index)] + stepTail[static_cast<std::size_t>(index + 1)]) / (2.0 * resolution);
	}

	struct Event { double phase, valueJump, slopeJump; };

	static std::array<Event, 4> events(int anchor, double width)
	{
		const auto d = std::clamp(width * 0.01, 0.05, 0.95);
		const auto left = 0.5 / d, right = 0.5 / (1.0 - d);
		switch (anchor)
		{
		case 0:
			return { Event { d, 0.0, -2.0 * std::numbers::pi * 0.81649658 * (right - left) },
				Event { 0.0, 0.0, 2.0 * std::numbers::pi * 0.81649658 * (left - right) } };
		case 1:
			return { Event { d * 0.5, 0.0, -8.0 * left }, Event { (1.0 + d) * 0.5, 0.0, 8.0 * right },
				Event { d, 0.0, -4.0 * (right - left) }, Event { 0.0, 0.0, 4.0 * (left - right) } };
		case 2:
			return { Event { d, 0.0, -2.0 * (right - left) }, Event { 0.0, 2.0, -2.0 * (left - right) } };
		default:
			return { Event { d, -2.0 * 0.57735027, 0.0 }, Event { 0.0, 2.0 * 0.57735027, 0.0 } };
		}
	}

	double anchor(double phase, double increment, int shape, double width) const
	{
		double value = monoWidthIdealAnchor(shape, phase, width);
		for (const auto& event : events(shape, width))
		{
			if (event.valueJump == 0.0 && event.slopeJump == 0.0) continue;
			const auto distance = radius * increment;
			const auto first = static_cast<int>(std::floor(event.phase - phase - distance));
			const auto last = static_cast<int>(std::ceil(event.phase - phase + distance));
			for (int cycle = first; cycle <= last; ++cycle)
			{
				const auto t = (phase - event.phase + cycle) / increment;
				if (std::abs(t) >= radius) continue;
				const auto tail = lookup(stepTail, std::abs(t));
				// Filtered step minus raw step; filtered ramp minus raw ramp.
				value += event.valueJump * (t < 0.0 ? tail : -tail)
					+ event.slopeJump * increment * lookup(rampTail, std::abs(t));
			}
		}
		return value;
	}

	double wave(double phase, double increment, double morph, double width) const
	{
		const auto segment = std::clamp(static_cast<int>(morph), 0, 2);
		const auto fraction = morph - segment;
		const auto sawWeight = [](double t) { return t * t * (2.0 - t); };
		const auto weight = segment == 0 ? fraction : segment == 1 ? sawWeight(fraction) : 1.0 - sawWeight(1.0 - fraction);
		const auto from = anchor(phase, increment, segment, width);
		return from + weight * (anchor(phase, increment, segment + 1, width) - from);
	}

private:
	double lookup(const std::vector<double>& table, double position) const
	{
		const auto location = position * resolution;
		const auto index = static_cast<std::size_t>(location);
		if (index + 1 >= table.size()) return 0.0;
		const auto fraction = location - static_cast<double>(index);
		return table[index] + fraction * (table[index + 1] - table[index]);
	}

	int radius, resolution;
	std::vector<double> stepTail, rampTail;
};
}