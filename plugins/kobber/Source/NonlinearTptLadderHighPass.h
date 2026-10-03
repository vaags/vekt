#pragma once

#include "LadderResonance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace vekt::mono
{
// The Ladder's high-pass (ADR 0009): a true high-pass ladder, four one-pole high-pass stages under global feedback, the
// low-pass ladder's mirror. Mode crossfades from the low-pass ladder's Notch into it across Notch -> HP (MonoVoice).
//
// The input saturates as a tanh(D x / a) with the SVF's knee a = 3, the stages are linear and only the feedback
// saturates: u_1 = v - k tanh(y_4), u_(i+1) = y_i. Small-signal, y_4 = HP^4 / (1 + k HP^4) v with HP = s / (s + w): the
// high band loses 1 / (1 + k) with Resonance like the low-pass ladder's bass (the SVF's and K35's Resonance trims follow
// that loss, so no level lift is needed), the peak at w is 1 / (4 - k), and since tanh adds no phase it would
// self-oscillate exactly at the cutoff from k = 4. The saturation acts only on the resonance, so a hot, detuned mix does
// not intermodulate into the jittery static that saturating stages produced, and Drive grits it like the SVF.
//
// Trapezoidal stages give y_i = (u_i - s_i) / (1 + g), so y_4 = A u_1 + B with A = (1 + g)^-4 and B the chain's response
// to u_1 = 0. Each sample is the scalar F(u_1) = u_1 + k tanh(A u_1 + B) - v = 0 with F' >= 1: a unique root in
// [v - k, v + k], found by safeguarded Newton with one tanh per step.
struct NonlinearTptLadderHighPassSettings
{
	double cutoffHz { 1'000.0 };
	double feedback {};      // k
	double driveDecibels {};
	double inputKnee { 3.0 }; // a in a tanh(D x / a)
	bool linear {};          // tanh(v) = v: the linear reference, development only
};

struct NonlinearTptLadderHighPassDiagnostics
{
	std::uint64_t samples {}, iterations {}, unconvergedSamples {}, nonFiniteSamples {};
	int maximumIterations {};
};

// The high-pass ladder's feedback for a Resonance r: the low-pass ladder's law with the top of the knob compressed so the
// high-pass stops just short of self-oscillation (Thomas preferred it in the Audio Lab audition; its oscillation
// interacting with a note sounded unsettled). r up to 90 %; above, 0.9 + 0.1 (u - 0.21 u^2) with u = (r - 0.9) / 0.1
// (slope 1 at 90 %, still rising at 100 %), so 100 % is 97.9 % (k = 3.916) and the knob keeps working.
[[nodiscard]] inline double ladderHighPassFeedback(double resonance) noexcept
{
	const auto r = std::clamp(resonance, 0.0, 1.0);
	if (r <= 0.9) return ladderFeedbackGain(r);
	const auto u = (r - 0.9) / 0.1;
	return ladderFeedbackGain(0.9 + 0.1 * (u - 0.21 * u * u));
}

class NonlinearTptLadderHighPass
{
public:
	using Stages = std::array<double, 4>;

	// A sample's coefficients, shared by filters at one rate and settings (MonoVoice computes them once per sample for
	// every unison layer).
	struct Coefficients
	{
		double g {}, scale { 1.0 }, a { 1.0 }, k {}, knee { 3.0 }, driveGain { 1.0 };
		bool linear {};
	};

	void prepare(double newSampleRate) noexcept
	{
		sampleRate = newSampleRate;
		reset();
	}

	void reset() noexcept { state = {}; }

	// Starts from the steady state for a constant input: the first stage's low-pass holds the (saturated) input and the
	// rest is silent, so a filter started on a running signal sees no step, and rings no more than a warm one.
	void prime(double input, const Coefficients& c) noexcept { state = { shapedInput(input, c), 0.0, 0.0, 0.0 }; }
	void prime(double input, const NonlinearTptLadderHighPassSettings& settings) noexcept { prime(input, coefficients(settings)); }

	[[nodiscard]] Coefficients coefficients(const NonlinearTptLadderHighPassSettings& settings) noexcept
	{
		Coefficients c;
		const auto cutoff = std::clamp(settings.cutoffHz, 2.5, 0.45 * sampleRate);
		c.g = std::tan(std::numbers::pi * cutoff / sampleRate);
		c.scale = 1.0 / (1.0 + c.g);
		c.a = c.scale * c.scale * c.scale * c.scale;
		c.k = std::max(0.0, settings.feedback);
		c.knee = std::max(1.0e-3, settings.inputKnee);
		c.driveGain = driveGainFor(settings.driveDecibels);
		c.linear = settings.linear;
		return c;
	}

	[[nodiscard]] const Stages& integratorState() const noexcept { return state; }
	void setIntegratorState(const Stages& newState) noexcept { state = newState; }

	[[nodiscard]] const NonlinearTptLadderHighPassDiagnostics& diagnostics() const noexcept { return diagnosticsState; }

	// The fourth stage's output.
	[[nodiscard]] double process(double input, const NonlinearTptLadderHighPassSettings& settings) noexcept
	{
		return process(input, coefficients(settings));
	}

	[[nodiscard]] double process(double input, const Coefficients& c) noexcept
	{
		constexpr int maximumIterations = 60;
		const auto g = c.g, scale = c.scale, a = c.a, k = c.k;
		const auto v = shapedInput(input, c);
		auto b = 0.0;
		for (const auto value : state) b = (b - value) * scale;
		auto u1 = (v - k * b) / (1.0 + k * a);
		int iterations {};
		bool converged = true;
		if (!c.linear && k > 0.0)
		{
			auto low = v - k, high = v + k;
			const auto tolerance = 1.0e-13 * (1.0 + std::abs(v) + k);
			u1 = std::clamp(u1, low, high);
			converged = false;
			for (; iterations < maximumIterations; ++iterations)
			{
				const auto shaped = std::tanh(a * u1 + b);
				const auto f = u1 + k * shaped - v;
				if (std::abs(f) <= tolerance || high - low <= tolerance)
				{
					converged = true;
					break;
				}
				if (f > 0.0) high = u1;
				else low = u1;
				const auto next = u1 - f / (1.0 + k * a * (1.0 - shaped * shaped));
				u1 = next > low && next < high ? next : 0.5 * (low + high);
			}
		}
		++diagnosticsState.samples;
		diagnosticsState.iterations += static_cast<std::uint64_t>(iterations);
		diagnosticsState.maximumIterations = std::max(diagnosticsState.maximumIterations, iterations);
		if (!converged) ++diagnosticsState.unconvergedSamples;
		auto u = u1;
		Stages lowPass {};
		for (std::size_t stage = 0; stage < 4; ++stage)
		{
			lowPass[stage] = (state[stage] + g * u) * scale;
			u -= lowPass[stage];
		}
		if (!std::isfinite(u))
		{
			++diagnosticsState.nonFiniteSamples;
			reset();
			return 0.0;
		}
		for (std::size_t stage = 0; stage < 4; ++stage) state[stage] = 2.0 * lowPass[stage] - state[stage];
		return u;
	}

private:
	[[nodiscard]] static double shapedInput(double input, const Coefficients& c) noexcept
	{
		const auto driven = input * c.driveGain;
		return c.linear ? driven : c.knee * std::tanh(driven / c.knee);
	}

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
	Stages state {};
	double cachedDriveDecibels {}, cachedDriveGain { 1.0 };
	NonlinearTptLadderHighPassDiagnostics diagnosticsState;
};
}
