#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

namespace vekt::dsp
{
struct LinearTptSvfOutputs
{
	double lowPass {}, bandPass {}, highPass {};
	[[nodiscard]] double notch() const noexcept { return lowPass + highPass; }
};

// The canonical two-integrator TPT state-variable filter (ADR 0006): HP = x - k BP - LP, HP -> integrator -> BP,
// BP -> integrator -> LP, with trapezoidal integrators prewarped so the pole frequency is exactly the cutoff.
// k is the damping, 1/Q; at the cutoff |LP| = |BP| = |HP| = 1/k and LP + HP has its notch. Kobber uses it as the linear
// reference for its nonlinear SVF; Flint for its click and noise filters.
class LinearTptSvf
{
public:
	void prepare(double newSampleRate) noexcept
	{
		sampleRate = newSampleRate;
		reset();
	}

	void reset() noexcept { bandState = lowState = 0.0; }

	[[nodiscard]] LinearTptSvfOutputs process(double input, double cutoffHz, double damping) noexcept
	{
		const auto cutoff = std::clamp(cutoffHz, 2.5, sampleRate * 0.45);
		const auto g = std::tan(std::numbers::pi * cutoff / sampleRate);
		const auto k = std::max(0.0, damping);
		LinearTptSvfOutputs out;
		out.highPass = (input - (k + g) * bandState - lowState) / (1.0 + k * g + g * g);
		const auto bandIncrement = g * out.highPass;
		out.bandPass = bandState + bandIncrement;
		bandState = out.bandPass + bandIncrement;
		const auto lowIncrement = g * out.bandPass;
		out.lowPass = lowState + lowIncrement;
		lowState = out.lowPass + lowIncrement;
		return out;
	}

private:
	double sampleRate { 48'000.0 };
	double bandState {}, lowState {};
};
}
