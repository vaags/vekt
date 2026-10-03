#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <utility>

namespace vekt::kobber
{
struct NonlinearTptSvfSettings
{
	double cutoffHz { 1'000.0 };
	double damping { 2.0 }; // k = 1/Q
	double driveDecibels {};
	double knee { 4.0 };    // a: input saturation a tanh(v / a), and the scale of the damping law
	double dampingCurve {}; // beta: psi(B) = B + beta B^3 / a^2; 0 is saturated input into a linear SVF
};

struct NonlinearTptSvfOutputs
{
	double lowPass {}, bandPass {}, highPass {};
	[[nodiscard]] double notch() const noexcept { return lowPass + highPass; } // u - k psi(B)
};

struct NonlinearTptSvfDiagnostics
{
	std::uint64_t samples {}, iterations {}, fallbackSteps {}, unconvergedSamples {}, nonFiniteSamples {};
	int maximumIterations {};
	double maximumResidual {};
};

// One sample's solve of the SVF with amplitude-increasing damping (ADR 0006). The continuous system is
//   u = a tanh(D x / a),   B' = w (u - k psi(B) - L),   L' = w B,   psi(B) = B + beta B^3 / a^2,  psi' >= 1
// The coupling between B and L is linear and skew, so two trajectories under the same input lose the energy of
// their difference as w k dB (psi(B1) - psi(B2)) >= w k dB^2: damping only grows with amplitude and states are
// driven together. With trapezoidal states, B = s1 + g (u - k psi(B) - L) and L = s2 + g B give
//   F(B) = (1 + g^2) B + g k psi(B) - C = 0,   C = s1 + g u - g s2,   F'(B) = 1 + g^2 + g k psi'(B) > 0
// so the root is unique, and since psi(B) has the sign of B it lies between 0 and C / (1 + g^2). Safeguarded Newton
// from the linear SVF's B, C / (1 + g^2 + g k); the nonlinearity is a cubic, so an iteration costs no tanh.
struct NonlinearTptSvfSolution
{
	double band {}, residual {};
	int iterations {}, fallbackSteps {};
	bool converged {};
};

[[nodiscard]] inline double nonlinearTptSvfSaturate(double value, double knee) noexcept
{
	return knee * std::tanh(value / knee);
}

[[nodiscard]] inline double nonlinearTptSvfDamping(double band, double knee, double curve) noexcept
{
	return band + curve * band * band * band / (knee * knee);
}

// F(B) for C = s1 + g u - g s2.
[[nodiscard]] inline double nonlinearTptSvfResidual(double band, double c, double g, double k, double knee, double curve) noexcept
{
	return (1.0 + g * g) * band + g * k * nonlinearTptSvfDamping(band, knee, curve) - c;
}

inline constexpr int nonlinearTptSvfMaximumIterations = 32;

[[nodiscard]] inline NonlinearTptSvfSolution solveNonlinearTptSvf(double c, double g, double k, double knee, double curve,
	double seed) noexcept
{
	const auto outer = c / (1.0 + g * g);
	auto low = std::min(0.0, outer), high = std::max(0.0, outer);
	// Floating-point residuals are relative to the size of the terms that cancel in F.
	const auto tolerance = 1.0e-12 * (1.0 + std::abs(c));
	NonlinearTptSvfSolution solution;
	auto band = std::clamp(std::isfinite(seed) ? seed : outer, low, high);
	auto step = high - low, previousStep = step;
	auto widthTwoAgo = std::numeric_limits<double>::infinity(), previousWidth = widthTwoAgo;
	for (int iteration = 1;; ++iteration)
	{
		solution.band = band;
		const auto f = nonlinearTptSvfResidual(band, c, g, k, knee, curve);
		solution.residual = std::abs(f);
		solution.iterations = iteration;
		solution.converged = solution.residual <= tolerance;
		if (solution.converged || iteration == nonlinearTptSvfMaximumIterations || !std::isfinite(f)) return solution;
		if (f > 0.0) high = band;
		else low = band;
		const auto bracketStalled = high - low > 0.5 * std::exchange(widthTwoAgo, std::exchange(previousWidth, high - low));
		const auto newtonStep = f / (1.0 + g * g + g * k * (1.0 + 3.0 * curve * band * band / (knee * knee)));
		auto next = band - newtonStep;
		if (next > high && next - high <= tolerance) next = high;
		if (next < low && low - next <= tolerance) next = low;
		// Bisect when Newton leaves the bracket, or stalls: more than half the step before last (rtsafe) while the
		// bracket has not halved over the last two iterations.
		const auto stepBeforeLast = std::exchange(previousStep, step);
		if (next >= low && next <= high && !(bracketStalled && 2.0 * std::abs(newtonStep) > std::abs(stepBeforeLast)))
		{
			step = newtonStep;
			band = next;
			continue;
		}
		step = 0.5 * (high - low);
		const auto middle = low + step;
		++solution.fallbackSteps;
		// The bracket has collapsed to adjacent doubles: B is the root as closely as it can be represented.
		if (middle <= low || middle >= high)
		{
			solution.converged = true;
			return solution;
		}
		band = middle;
	}
}

// Two-pole nonlinear TPT state-variable filter (ADR 0006): saturated input u = a tanh(D x / a) into an SVF whose
// damping grows with amplitude, psi(B) = B + beta B^3 / a^2, so resonance compresses as the filter works harder and
// forced trajectories converge. Heard outputs are the states: LP = L, BP = B, HP = u - k psi(B) - L, so HP tends to
// the bounded u, never the raw driven input. At small signals this is LinearTptSvf with Drive gain.
class NonlinearTptSvf
{
public:
	void prepare(double newSampleRate) noexcept
	{
		sampleRate = newSampleRate;
		reset();
	}

	void reset() noexcept { bandState = lowState = 0.0; }

	[[nodiscard]] NonlinearTptSvfOutputs process(double input, const NonlinearTptSvfSettings& settings) noexcept
	{
		const auto cutoff = std::clamp(settings.cutoffHz, 2.5, sampleRate * 0.45);
		const auto g = std::tan(std::numbers::pi * cutoff / sampleRate);
		const auto k = std::max(0.0, settings.damping);
		const auto knee = std::max(1.0e-3, settings.knee);
		const auto u = nonlinearTptSvfSaturate(input * driveGainFor(settings.driveDecibels), knee);
		const auto curve = std::max(0.0, settings.dampingCurve);
		const auto c = bandState + g * (u - lowState);
		const auto solution = solveNonlinearTptSvf(c, g, k, knee, curve, c / (1.0 + g * (g + k)));
		++solverDiagnostics.samples;
		solverDiagnostics.iterations += static_cast<std::uint64_t>(solution.iterations);
		solverDiagnostics.fallbackSteps += static_cast<std::uint64_t>(solution.fallbackSteps);
		solverDiagnostics.maximumIterations = std::max(solverDiagnostics.maximumIterations, solution.iterations);
		solverDiagnostics.maximumResidual = std::max(solverDiagnostics.maximumResidual, solution.residual);
		if (!solution.converged) ++solverDiagnostics.unconvergedSamples;
		const auto band = solution.band;
		const auto low = lowState + g * band;
		if (!std::isfinite(band) || !std::isfinite(low))
		{
			++solverDiagnostics.nonFiniteSamples;
			reset();
			return {};
		}
		// The trapezoidal state update, once, from the solved states: s <- 2 y - s.
		bandState = 2.0 * band - bandState;
		lowState = 2.0 * low - lowState;
		return { low, band, u - k * nonlinearTptSvfDamping(band, knee, curve) - low };
	}

	[[nodiscard]] const NonlinearTptSvfDiagnostics& diagnostics() const noexcept { return solverDiagnostics; }

private:
	// Drive in dB to linear gain, recomputed only when Drive changes.
	[[nodiscard]] double driveGainFor(double decibels) noexcept
	{
		if (std::abs(decibels - cachedDriveDecibels) > 0.0)
		{
			cachedDriveDecibels = decibels;
			cachedDriveGain = std::pow(10.0, decibels / 20.0);
		}
		return cachedDriveGain;
	}

	double sampleRate { 48'000.0 };
	double bandState {}, lowState {};
	double cachedDriveDecibels {}, cachedDriveGain { 1.0 };
	NonlinearTptSvfDiagnostics solverDiagnostics;
};
}
