#pragma once

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <span>
#include <tuple>
#include <utility>

#include "SimdLanes.h"

namespace vekt::mono
{
// A reduced model of the early (Korg35-based) MS-20 low-pass, Mono's K35 filter (ADR 0007). It follows
// Stinchcombe's simplified Korg35 structure (A Study of the Korg MS10 & MS20 Filters, 2006, figure 3 and section 5):
// a Sallen-Key pair with C1 = 3 C2 and R1 = R2 / 3, whose output gain stage (x58 non-inverting, back-to-back diodes)
// is a limiter in the forward path, and whose output also drives the resonance through C1. Not a circuit emulation:
// no transistor-dependent cutoff modulation or asymmetry, no component tolerances.
//
// Normalised to unity small-signal passband, with states scaled by the stage gain G, input x = D x_in, loop gain rho:
//   v = U1 + rho h(U2),   U1' = w ((x - v) + (U2 - v) / 3),   U2' = w (v - U2),   output y = h(U2)
//   h(u) = u / G + (1 - 1 / G) K tanh(u / K)
// h has unit slope at 0 and slope 1 / G for |u| >> K: the diode stage, whose gain falls from G to unity once the diodes
// conduct. Small-signal LP is 1 / (p^2 + (7/3 - rho) p + 1) (Stinchcombe's equation 8), self-oscillating from
// rho = 7/3; the circuit's nominal maximum is about 2.2.
//
// Below threshold the incremental dynamics in w = dU2, v = dU1 + 4/3 dU2 are exactly
//   w' = w0 ((c - 7/3) w + v),   v' = -w0 w,   c = rho h'(U2) in [rho / G, rho],
// a damped oscillator whose damping 7/3 - c stays positive for rho < 7/3, so forced trajectories converge.
//
// Mode's high-pass (ADR 0007) is how the MS-20 makes its high-pass from the same circuit (Stinchcombe, section 7): the
// input goes into the lifted ground end of C2 instead of the low-pass input. highPass = b feeds x_lp = (1 - b) x and
// x_hp = b x; C2's state becomes W = U2 - x_hp, W' = w0 (v - U2). Small-signal,
//   y = [x_lp + (p^2 + 4/3 p) x_hp] / (p^2 + (7/3 - rho) p + 1),
// so b = 1 is a 6 dB/oct high-pass with the same poles (same resonance and threshold), and b = 1/2 is the full signal
// at -6 dB plus a resonant bell (flat at rho = 1). Per sample this is the low-pass solve with s2 + x_hp in place of s2,
// so the residual, its unique root and the solver are unchanged.
struct NonlinearTptKorg35Settings
{
	double cutoffHz { 1'000.0 };
	double feedback { 1.0 }; // rho, the small-signal loop gain k1 k2; threshold 7/3
	double driveDecibels {};
	double knee { 1.0 };       // K: the output level where the diodes take over
	double stageGain { 58.0 }; // G: small-signal over large-signal gain of the diode stage
	double highPass {};        // b in [0, 1]: share of the driven input fed into C2 (the high-pass) rather than the LP input
	bool linear {};            // h(u) = u: the linear reference, development only
};

struct NonlinearTptKorg35Diagnostics
{
	std::uint64_t samples {}, iterations {}, fallbackSteps {}, unconvergedSamples {}, nonFiniteSamples {};
	int maximumIterations {};
	double maximumResidual {};
};

inline constexpr int nonlinearTptKorg35MaximumIterations = 32;
inline constexpr double nonlinearTptKorg35Threshold = 7.0 / 3.0;
inline constexpr double nonlinearTptKorg35MaximumFeedback = 4.3; // the solve is unique for rho < 13/3

struct NonlinearTptKorg35Stage
{
	double value {}, slope {};
};

[[nodiscard]] inline NonlinearTptKorg35Stage nonlinearTptKorg35Stage(double input, double knee, double gain) noexcept
{
	const auto limited = std::tanh(input / knee);
	const auto share = 1.0 - 1.0 / gain;
	return { input / gain + share * knee * limited, 1.0 / gain + share * (1.0 - limited * limited) };
}

// One sample's solve. With trapezoidal states s1, s2 and the unknown e = v - U2 (so U2 = s2 + g e, v = U2 + e, and
// U1 = v - rho h(U2) must equal s1 + g (x - v - e / 3)):
//   R(e) = A e + (1 + g) s2 - s1 - g x - rho h(s2 + g e) = 0,   A = (1 + g)^2 + g / 3 = 1 + 7g/3 + g^2
//   R'(e) = A - g rho h'(U2) >= 1 + (7/3 - rho) g + g^2 > 0 for rho < 13/3
// so the root is unique. Splitting h into its linear part and the bounded tanh part puts it inside
// [C - B, C + B] / (A - g rho / G), C = s1 + g x - (1 + g - rho / G) s2, B = rho (1 - 1 / G) K.
struct NonlinearTptKorg35Solution
{
	double difference {}, residual {};
	int iterations {}, fallbackSteps {};
	bool converged {};
};

[[nodiscard]] inline double nonlinearTptKorg35Residual(double difference, double s1, double s2, double g, double feedback, double driven,
	double knee, double gain) noexcept
{
	const auto a = (1.0 + g) * (1.0 + g) + g / 3.0;
	return a * difference + (1.0 + g) * s2 - s1 - g * driven - feedback * nonlinearTptKorg35Stage(s2 + g * difference, knee, gain).value;
}

[[nodiscard]] inline std::pair<double, double> nonlinearTptKorg35Bracket(double s1, double s2, double g, double feedback, double driven,
	double knee, double gain) noexcept
{
	const auto slope = (1.0 + g) * (1.0 + g) + g / 3.0 - g * feedback / gain;
	const auto c = s1 + g * driven - (1.0 + g - feedback / gain) * s2;
	const auto b = feedback * (1.0 - 1.0 / gain) * knee;
	return { (c - b) / slope, (c + b) / slope };
}

// The linear TPT solution (h(u) = u), the solve's seed.
[[nodiscard]] inline double nonlinearTptKorg35LinearDifference(double s1, double s2, double g, double feedback, double driven) noexcept
{
	return (s1 + g * driven - (1.0 + g - feedback) * s2) / ((1.0 + g) * (1.0 + g) + g / 3.0 - g * feedback);
}

// One lane's safeguarded Newton solve, advanced one residual evaluation at a time: argument() is the point's tanh
// argument, advance() takes its tanh. The scalar solve and the batched lanes (tanh shared across lanes) run exactly
// this logic, so they differ only in how tanh is evaluated.
struct NonlinearTptKorg35Newton
{
	double a {}, s1 {}, s2 {}, g {}, feedback {}, driven {}, knee {}, gain {};
	double low {}, high {}, tolerance {}, difference {}, step {}, previousStep {}, widthTwoAgo {}, previousWidth {};
	int iteration {};
	bool done {};
	NonlinearTptKorg35Solution solution;
	double stageValue {}; // h at solution.difference, the last point evaluated

	void start(double firstState, double secondState, double coefficient, double loopGain, double drivenInput, double kneeLevel,
		double stageGain, double seed) noexcept
	{
		s1 = firstState;
		s2 = secondState;
		g = coefficient;
		feedback = loopGain;
		driven = drivenInput;
		knee = kneeLevel;
		gain = stageGain;
		a = (1.0 + g) * (1.0 + g) + g / 3.0;
		std::tie(low, high) = nonlinearTptKorg35Bracket(s1, s2, g, feedback, driven, knee, gain);
		// Residuals are relative to the size of the terms that cancel in R.
		tolerance = 1.0e-12 * (1.0 + std::abs(s1) + (1.0 + g) * std::abs(s2) + g * std::abs(driven) + feedback * knee);
		solution = {};
		difference = std::clamp(std::isfinite(seed) ? seed : 0.5 * (low + high), low, high);
		step = high - low;
		previousStep = step;
		widthTwoAgo = std::numeric_limits<double>::infinity();
		previousWidth = widthTwoAgo;
		iteration = 0;
		done = false;
	}

	[[nodiscard]] double argument() const noexcept { return (s2 + g * difference) / knee; }

	void advance(double limited) noexcept
	{
		++iteration;
		solution.difference = difference;
		const auto input = s2 + g * difference;
		const auto share = 1.0 - 1.0 / gain;
		stageValue = input / gain + share * knee * limited;
		const auto slope = 1.0 / gain + share * (1.0 - limited * limited);
		const auto r = a * difference + (1.0 + g) * s2 - s1 - g * driven - feedback * stageValue;
		solution.residual = std::abs(r);
		solution.iterations = iteration;
		solution.converged = solution.residual <= tolerance;
		if (solution.converged || iteration == nonlinearTptKorg35MaximumIterations || !std::isfinite(r))
		{
			done = true;
			return;
		}
		if (r > 0.0) high = difference;
		else low = difference;
		const auto bracketStalled = high - low > 0.5 * std::exchange(widthTwoAgo, std::exchange(previousWidth, high - low));
		const auto newtonStep = r / (a - g * feedback * slope);
		auto next = difference - newtonStep;
		if (next > high && next - high <= tolerance) next = high;
		if (next < low && low - next <= tolerance) next = low;
		// As in the SVF solve (ADR 0006): bisect when Newton leaves the bracket, or stalls.
		const auto stepBeforeLast = std::exchange(previousStep, step);
		if (next >= low && next <= high && !(bracketStalled && 2.0 * std::abs(newtonStep) > std::abs(stepBeforeLast)))
		{
			step = newtonStep;
			difference = next;
			return;
		}
		step = 0.5 * (high - low);
		const auto middle = low + step;
		++solution.fallbackSteps;
		// The bracket has collapsed to adjacent doubles: d is the root as closely as it can be represented.
		if (middle <= low || middle >= high)
		{
			solution.converged = true;
			done = true;
			return;
		}
		difference = middle;
	}
};

[[nodiscard]] inline NonlinearTptKorg35Solution solveNonlinearTptKorg35(double s1, double s2, double g, double feedback, double driven,
	double knee, double gain, double seed) noexcept
{
	NonlinearTptKorg35Newton newton;
	newton.start(s1, s2, g, feedback, driven, knee, gain, seed);
	while (!newton.done) newton.advance(std::tanh(newton.argument()));
	return newton.solution;
}

class NonlinearTptKorg35
{
public:
	void prepare(double newSampleRate) noexcept
	{
		sampleRate = newSampleRate;
		reset();
	}

	void reset() noexcept { firstState = secondState = 0.0; }

	// The trapezoidal states s1, s2, for measurements. Like the Ladder, the filter never leaves an exact zero state by
	// itself above threshold.
	[[nodiscard]] std::pair<double, double> state() const noexcept { return { firstState, secondState }; }
	void setState(double first, double second) noexcept
	{
		firstState = first;
		secondState = second;
	}

	// The output y = h(U2).
	[[nodiscard]] double process(double input, const NonlinearTptKorg35Settings& settings) noexcept
	{
		const auto sample = begin(input, settings, nullptr);
		if (settings.linear) return finish(sample, nullptr, 0.0);
		NonlinearTptKorg35Newton newton;
		newton.start(firstState, sample.base, sample.g, sample.feedback, sample.driven, sample.knee, sample.gain, sample.linearDifference);
		while (!newton.done) newton.advance(std::tanh(newton.argument()));
		return finish(sample, &newton.solution, newton.stageValue);
	}

	// Several filters at once, each with its own input, output and settings (e.g. every sounding voice's unison layers):
	// lanes are grouped four, then two, then one at a time and their Newton solves share a vector tanh (Apple simd; about
	// 2 ulp from libm, as in the ladder's batched solve, and the same bits for a lane however lanes are grouped); a lane
	// in linear mode uses process. Every lane keeps its own solve, convergence and diagnostics. Filters must not repeat.
	static void processLanes(std::span<NonlinearTptKorg35* const> filters, std::span<const double> inputs, std::span<double> outputs,
		std::span<const NonlinearTptKorg35Settings* const> settings) noexcept
	{
		std::size_t lane {};
		const auto anyLinear = [&](std::size_t from, std::size_t count)
		{
			for (auto index = from; index < from + count; ++index)
				if (settings[index]->linear) return true;
			return false;
		};
		while (lane < filters.size())
		{
			const auto remaining = filters.size() - lane;
			if (remaining >= 4 && !anyLinear(lane, 4))
			{
				processGroup<4>(filters.subspan(lane, 4), inputs.subspan(lane, 4), outputs.subspan(lane, 4), settings.subspan(lane, 4));
				lane += 4;
			}
			else if (remaining >= 2 && !anyLinear(lane, 2))
			{
				processGroup<2>(filters.subspan(lane, 2), inputs.subspan(lane, 2), outputs.subspan(lane, 2), settings.subspan(lane, 2));
				lane += 2;
			}
			else if (!settings[lane]->linear)
			{
				processGroup<1>(filters.subspan(lane, 1), inputs.subspan(lane, 1), outputs.subspan(lane, 1), settings.subspan(lane, 1));
				++lane;
			}
			else
			{
				outputs[lane] = filters[lane]->process(inputs[lane], *settings[lane]);
				++lane;
			}
		}
	}

	[[nodiscard]] const NonlinearTptKorg35Diagnostics& diagnostics() const noexcept { return diagnosticsState; }

private:
	// One sample's coefficients and linear seed, before the solve.
	struct Sample
	{
		// driven: the low-pass input x_lp; high: the high-pass input x_hp; base: s2 + x_hp, the solve's second state.
		double cutoff {}, g {}, feedback {}, knee {}, gain {}, driven {}, high {}, base {}, linearDifference {};
	};

	// shared: the same sample's coefficients from a filter at the same rate with the same settings (a voice's unison
	// layers), whose tan need not be evaluated again.
	[[nodiscard]] Sample begin(double input, const NonlinearTptKorg35Settings& settings, const Sample* shared) noexcept
	{
		Sample sample;
		sample.cutoff = std::clamp(settings.cutoffHz, 2.5, sampleRate * 0.45);
		sample.g = shared != nullptr ? shared->g : std::tan(std::numbers::pi * sample.cutoff / sampleRate);
		sample.feedback = std::clamp(settings.feedback, 0.0, nonlinearTptKorg35MaximumFeedback);
		sample.knee = std::max(1.0e-3, settings.knee);
		sample.gain = std::max(1.0, settings.stageGain);
		const auto driven = input * driveGainFor(settings.driveDecibels);
		const auto blend = std::clamp(settings.highPass, 0.0, 1.0);
		// At b = 0 exactly the low-pass arithmetic, so a plain low-pass renders the same bits.
		sample.driven = blend > 0.0 ? (1.0 - blend) * driven : driven;
		sample.high = blend > 0.0 ? blend * driven : 0.0;
		sample.base = blend > 0.0 ? secondState + sample.high : secondState;
		sample.linearDifference = nonlinearTptKorg35LinearDifference(firstState, sample.base, sample.g, sample.feedback, sample.driven);
		return sample;
	}

	// The state update from the solved point (solution null: the linear reference). stageValue is h at the solution,
	// the solve's last evaluation, so the output needs no further tanh.
	[[nodiscard]] double finish(const Sample& sample, const NonlinearTptKorg35Solution* solution, double stageValue) noexcept
	{
		++diagnosticsState.samples;
		auto difference = sample.linearDifference;
		if (solution != nullptr)
		{
			diagnosticsState.iterations += static_cast<std::uint64_t>(solution->iterations);
			diagnosticsState.fallbackSteps += static_cast<std::uint64_t>(solution->fallbackSteps);
			diagnosticsState.maximumIterations = std::max(diagnosticsState.maximumIterations, solution->iterations);
			diagnosticsState.maximumResidual = std::max(diagnosticsState.maximumResidual, solution->residual);
			if (!solution->converged) ++diagnosticsState.unconvergedSamples;
			difference = solution->difference;
		}
		const auto second = sample.base + sample.g * difference;
		const auto output = solution == nullptr ? second : stageValue;
		const auto first = second + difference - sample.feedback * output;
		if (!std::isfinite(difference) || !std::isfinite(output) || !std::isfinite(first))
		{
			++diagnosticsState.nonFiniteSamples;
			reset();
			return 0.0;
		}
		// The trapezoidal state update, once, from the solved states: s <- 2 y - s.
		firstState = 2.0 * first - firstState;
		secondState = 2.0 * (sample.high != 0.0 ? second - sample.high : second) - secondState;
		return output;
	}

	template <std::size_t Lanes>
	static void processGroup(std::span<NonlinearTptKorg35* const> filters, std::span<const double> inputs, std::span<double> outputs,
		std::span<const NonlinearTptKorg35Settings* const> settings) noexcept
	{
		std::array<Sample, Lanes> samples;
		std::array<NonlinearTptKorg35Newton, Lanes> newtons;
		for (std::size_t lane = 0; lane < Lanes; ++lane)
		{
			const Sample* shared {};
			for (std::size_t earlier = 0; earlier < lane && shared == nullptr; ++earlier)
				if (settings[earlier] == settings[lane]
					&& std::bit_cast<std::uint64_t>(filters[earlier]->sampleRate) == std::bit_cast<std::uint64_t>(filters[lane]->sampleRate))
					shared = &samples[earlier];
			auto& filter = *filters[lane];
			samples[lane] = filter.begin(inputs[lane], *settings[lane], shared);
			const auto& sample = samples[lane];
			newtons[lane].start(filter.firstState, sample.base, sample.g, sample.feedback, sample.driven, sample.knee, sample.gain,
				sample.linearDifference);
		}
		for (;;)
		{
			SimdLaneValues<Lanes> arguments {};
			bool pending {};
			for (std::size_t lane = 0; lane < Lanes; ++lane)
				if (!newtons[lane].done)
				{
					arguments[lane] = newtons[lane].argument();
					pending = true;
				}
			if (!pending) break;
			const auto limited = tanhSimdLanes<Lanes>(arguments);
			for (std::size_t lane = 0; lane < Lanes; ++lane)
				if (!newtons[lane].done) newtons[lane].advance(limited[lane]);
		}
		for (std::size_t lane = 0; lane < Lanes; ++lane)
			outputs[lane] = filters[lane]->finish(samples[lane], &newtons[lane].solution, newtons[lane].stageValue);
	}

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
	double firstState {}, secondState {};
	double cachedDriveDecibels {}, cachedDriveGain { 1.0 };
	NonlinearTptKorg35Diagnostics diagnosticsState;
};
}
