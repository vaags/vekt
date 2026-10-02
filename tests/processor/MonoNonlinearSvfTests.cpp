#include "FilterPrototypeSupport.h"
#include "LinearTptSvf.h"
#include "NonlinearTptLadder.h"
#include "NonlinearTptSvf.h"
#include "SvfResponse.h"

#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numbers>
#include <random>
#include <string>
#include <vector>

namespace
{
using vekt::mono::LinearTptSvf;
using vekt::mono::NonlinearTptSvf;
using vekt::mono::NonlinearTptSvfSettings;
using vekt::mono::nonlinearTptSvfMaximumIterations;
using vekt::mono::nonlinearTptSvfResidual;
using vekt::mono::solveNonlinearTptSvf;

// A random solver problem over the whole operating range: states well beyond any passband level, the saturated
// input, g from the 2.5 Hz floor at 1.536 MHz to 0.45 fs, all damping the SVF allows, knees and damping curves around the
// voicing.
struct Problem
{
	double c, g, k, knee, curve; // c = s1 + g u - g s2
};

Problem randomProblem(std::mt19937& random)
{
	std::uniform_real_distribution unit { 0.0, 1.0 };
	const auto between = [&](double low, double high) { return low + (high - low) * unit(random); };
	constexpr std::array knees { 1.0, 4.0, 8.0 };
	constexpr std::array curves { 0.0, 0.25, 0.5, 1.0, 4.0 };
	const auto knee = knees[random() % knees.size()];
	const auto g = std::exp(between(std::log(5.0e-6), std::log(6.4)));
	const auto u = vekt::mono::nonlinearTptSvfSaturate(between(-24.0, 24.0), knee);
	return { between(-40.0, 40.0) + g * (u - between(-40.0, 40.0)), g, between(0.0, 2.0), knee, curves[random() % curves.size()] };
}

std::pair<double, double> bracket(const Problem& p)
{
	const auto outer = p.c / (1.0 + p.g * p.g);
	return { std::min(0.0, outer), std::max(0.0, outer) };
}

double residual(const Problem& p, double band) { return nonlinearTptSvfResidual(band, p.c, p.g, p.k, p.knee, p.curve); }

// The scale of the terms that cancel in F, for rounding allowances.
double residualScale(const Problem& p) { return 1.0e-12 * (1.0 + std::abs(p.c)); }

double linearSeed(const Problem& p) { return p.c / (1.0 + p.g * (p.g + p.k)); }

// rms(y[n + N] - y[n]) / rms(y): zero for output that repeats with the input's period N.
double nonPeriodicity(const std::vector<double>& y, int period)
{
	double difference {}, energy {};
	for (std::size_t sample = 0; sample + static_cast<std::size_t>(period) < y.size(); ++sample)
	{
		const auto delta = y[sample + static_cast<std::size_t>(period)] - y[sample];
		difference += delta * delta;
		energy += y[sample] * y[sample];
	}
	return energy > 0.0 ? std::sqrt(difference / energy) : 0.0;
}

enum class Wave { sine, saw, square };

// Whether an exactly periodic input leaves the SVF on a persistent non-periodic attractor: the LP, BP or HP output
// is still non-periodic 2 s in after being so at 1 s (slow transients die out in between). level is relative to
// the knee; the model is scale-equivariant, so this is what matters, not the knee or Drive separately.
bool persistentlyNonPeriodic(double sampleRate, double cutoff, double q, double ratio, Wave wave, double level)
{
	const auto period = static_cast<int>(std::lround(sampleRate / (cutoff * ratio)));
	NonlinearTptSvf svf;
	svf.prepare(sampleRate);
	const NonlinearTptSvfSettings settings { cutoff, 1.0 / q, 0.0, vekt::mono::svfKnee, vekt::mono::svfDampingCurve };
	std::array<std::array<std::vector<double>, 3>, 2> windows;
	const auto windowStart = [&](int index) { return static_cast<long>((index + 1) * sampleRate); };
	const auto end = windowStart(1) + 20L * period;
	for (long sample = 0; sample < end; ++sample)
	{
		const auto phase = static_cast<double>(sample % period) / period;
		const auto input = wave == Wave::sine ? std::sin(2.0 * std::numbers::pi * phase)
			: wave == Wave::saw ? 2.0 * phase - 1.0 : (phase < 0.5 ? 1.0 : -1.0);
		const auto out = svf.process(level * vekt::mono::svfKnee * input, settings);
		for (int index = 0; index < 2; ++index)
			if (sample >= windowStart(index) && sample < windowStart(index) + 20L * period)
			{
				auto& window = windows[static_cast<std::size_t>(index)];
				window[0].push_back(out.lowPass);
				window[1].push_back(out.bandPass);
				window[2].push_back(out.highPass);
			}
	}
	const auto at = [&](int index)
	{
		double worst {};
		for (const auto& output : windows[static_cast<std::size_t>(index)]) worst = std::max(worst, nonPeriodicity(output, period));
		return worst;
	};
	const auto first = at(0), second = at(1);
	return first > 1.0e-6 && second > 1.0e-6 && second > 0.3 * first;
}
}

TEST_CASE("Mono nonlinear SVF residual is strictly increasing with its root inside the analytic bracket", "[mono][filter][svf][svf-nonlinear]")
{
	std::mt19937 random { 11 };
	std::uniform_real_distribution unit { 0.0, 1.0 };
	for (int trial = 0; trial < 20'000; ++trial)
	{
		const auto p = randomProblem(random);
		const auto [low, high] = bracket(p);
		INFO("c " << p.c << " g " << p.g << " k " << p.k << " a " << p.knee << " beta " << p.curve);
		const auto scale = residualScale(p);
		REQUIRE(residual(p, low) <= scale);
		REQUIRE(residual(p, high) >= -scale);
		// F' >= 1 + g^2: any two points differ in F by at least that times their distance (up to rounding).
		const auto first = low - 5.0 + (high - low + 10.0) * unit(random);
		const auto second = low - 5.0 + (high - low + 10.0) * unit(random);
		REQUIRE((residual(p, std::max(first, second)) - residual(p, std::min(first, second)))
			>= (1.0 + p.g * p.g) * std::abs(first - second) - 1.0e-9 * (1.0 + std::abs(first) + std::abs(second)) * (1.0 + p.g * p.g + p.g * p.k));
	}
}

TEST_CASE("Mono nonlinear SVF solve converges from the linear seed within a few iterations", "[mono][filter][svf][svf-nonlinear]")
{
	std::mt19937 random { 12 };
	int worstIterations {};
	for (int trial = 0; trial < 50'000; ++trial)
	{
		const auto p = randomProblem(random);
		const auto [low, high] = bracket(p);
		const auto solution = solveNonlinearTptSvf(p.c, p.g, p.k, p.knee, p.curve, linearSeed(p));
		INFO("c " << p.c << " g " << p.g << " k " << p.k << " a " << p.knee << " beta " << p.curve);
		REQUIRE(solution.converged);
		REQUIRE(std::isfinite(solution.band));
		REQUIRE(solution.band >= low);
		REQUIRE(solution.band <= high);
		REQUIRE(std::abs(residual(p, solution.band)) <= residualScale(p));
		REQUIRE(solution.iterations < nonlinearTptSvfMaximumIterations);
		worstIterations = std::max(worstIterations, solution.iterations);
	}
	// Quadratic convergence of a monotone cubic from the linear seed: a small constant, not a search.
	CHECK(worstIterations <= 12);
}

TEST_CASE("Mono nonlinear SVF solve converges from either bracket end", "[mono][filter][svf][svf-nonlinear]")
{
	// F is an odd cubic, convex on the root's side of 0, with the root between 0 and C / (1 + g^2): Newton seeded
	// anywhere in that bracket stays in it, so the bisection safeguard is never needed in practice (it stays as a
	// guard). Seeded at either end, the solve must still converge within the iteration bound.
	std::mt19937 random { 13 };
	std::uint64_t fallbackSteps {};
	for (int trial = 0; trial < 5'000; ++trial)
	{
		const auto p = randomProblem(random);
		const auto [low, high] = bracket(p);
		for (const auto seed : { low, high })
		{
			const auto solution = solveNonlinearTptSvf(p.c, p.g, p.k, p.knee, p.curve, seed);
			REQUIRE(solution.converged);
			REQUIRE(std::abs(residual(p, solution.band)) <= residualScale(p));
			REQUIRE(solution.iterations < nonlinearTptSvfMaximumIterations);
			fallbackSteps += static_cast<std::uint64_t>(solution.fallbackSteps);
		}
	}
	CHECK(fallbackSteps == 0);
}

TEST_CASE("Mono nonlinear SVF converges to the linear SVF at low level", "[mono][filter][svf][svf-nonlinear]")
{
	// At 1e-5 input the input saturation and the cubic damping differ from linear by parts in 1e10 even at the
	// top Q's resonant gain, far below the 1e-6 tolerance.
	constexpr double amplitude = 1.0e-5;
	for (const auto hostRate : { 44'100.0, 48'000.0, 96'000.0, 192'000.0 })
		for (const auto factor : { 1, 2, 4, 8 })
			for (const auto cutoff : { 5.0, 1'000.0, 15'000.0 })
				for (const auto k : { 2.0, 0.5, vekt::mono::svfDamping(1.0) })
				{
					const auto sampleRate = hostRate * factor;
					LinearTptSvf linear;
					NonlinearTptSvf nonlinear;
					linear.prepare(sampleRate);
					nonlinear.prepare(sampleRate);
					std::mt19937 random { 14 };
					std::uniform_real_distribution noise { -amplitude, amplitude };
					double peak {}, lowError {}, highError {}, notchError {}, bandError {};
					for (int sample = 0; sample < 4'096; ++sample)
					{
						const auto input = noise(random);
						const auto reference = linear.process(input, cutoff, k);
						const auto out = nonlinear.process(input, { cutoff, k, 0.0, vekt::mono::svfKnee, vekt::mono::svfDampingCurve });
						peak = std::max({ peak, std::abs(reference.lowPass), std::abs(reference.highPass), std::abs(reference.bandPass) });
						lowError = std::max(lowError, std::abs(out.lowPass - reference.lowPass));
						bandError = std::max(bandError, std::abs(out.bandPass - reference.bandPass));
						highError = std::max(highError, std::abs(out.highPass - reference.highPass));
						notchError = std::max(notchError, std::abs(out.notch() - reference.notch()));
					}
					INFO("rate " << hostRate << " x" << factor << ", cutoff " << cutoff << " Hz, k " << k);
					REQUIRE(peak > 0.0);
					CHECK(lowError <= 1.0e-6 * peak);
					CHECK(bandError <= 1.0e-6 * peak);
					CHECK(highError <= 1.0e-6 * peak);
					CHECK(notchError <= 1.0e-6 * peak);
					CHECK(nonlinear.diagnostics().unconvergedSamples == 0);
					CHECK(nonlinear.diagnostics().nonFiniteSamples == 0);
				}
}

TEST_CASE("Mono nonlinear SVF decays to silence at maximum Resonance and Drive under cutoff modulation", "[mono][filter][svf][svf-nonlinear]")
{
	// ADR 0006's no-self-oscillation invariant over host rate x oversampling: excite hard, then with zero input and
	// the cutoff still sweeping two octaves either side of 1 kHz at 5 Hz, the tail must fall monotonically (per 10 ms
	// window, small slack for modulation moving energy between the states) to silence, with no solver failures.
	const NonlinearTptSvfSettings maximum { 1'000.0, vekt::mono::svfDamping(1.0), 24.0, vekt::mono::svfKnee, vekt::mono::svfDampingCurve };
	for (const auto hostRate : { 44'100.0, 48'000.0, 96'000.0, 192'000.0 })
		for (const auto factor : { 1, 2, 4, 8 })
		{
			const auto sampleRate = hostRate * factor;
			INFO("rate " << hostRate << " x" << factor);
			NonlinearTptSvf svf;
			svf.prepare(sampleRate);
			std::mt19937 random { 15 };
			std::uniform_real_distribution noise { -1.0, 1.0 };
			auto settings = maximum;
			const auto excitation = static_cast<long>(0.05 * sampleRate);
			const auto window = static_cast<long>(0.01 * sampleRate);
			const auto total = excitation + static_cast<long>(1.5 * sampleRate);
			double windowPeak {}, previousPeak = std::numeric_limits<double>::infinity(), finalPeak {};
			bool monotonic = true;
			for (long sample = 0; sample < total; ++sample)
			{
				settings.cutoffHz = 1'000.0 * std::exp2(2.0 * std::sin(2.0 * std::numbers::pi * 5.0 * static_cast<double>(sample) / sampleRate));
				const auto out = svf.process(sample < excitation ? noise(random) : 0.0, settings);
				if (sample < excitation) continue;
				windowPeak = std::max({ windowPeak, std::abs(out.lowPass), std::abs(out.bandPass), std::abs(out.highPass) });
				if ((sample - excitation + 1) % window != 0) continue;
				if (windowPeak > 1.0e-12 && windowPeak > 1.05 * previousPeak) monotonic = false;
				previousPeak = windowPeak;
				finalPeak = windowPeak;
				windowPeak = 0.0;
			}
			CHECK(monotonic);
			CHECK(finalPeak < 1.0e-9);
			CHECK(svf.diagnostics().unconvergedSamples == 0);
			CHECK(svf.diagnostics().nonFiniteSamples == 0);
			CHECK(svf.diagnostics().maximumIterations < nonlinearTptSvfMaximumIterations);
		}
}

TEST_CASE("Mono nonlinear SVF still decays at the lowest cutoff and highest effective rate", "[mono][filter][svf][svf-nonlinear]")
{
	// The 2.5 Hz floor at 192 kHz x8, where g is smallest: the envelope time constant is Q / (pi fc), about 2.5 s at
	// Q 20, so 7.5 seconds of tail should shed well over 90 % of its peak, without solver failures.
	constexpr double sampleRate = 192'000.0 * 8.0;
	NonlinearTptSvf svf;
	svf.prepare(sampleRate);
	const NonlinearTptSvfSettings settings { 2.5, vekt::mono::svfDamping(1.0), 24.0, vekt::mono::svfKnee, vekt::mono::svfDampingCurve };
	std::mt19937 random { 16 };
	std::uniform_real_distribution noise { -1.0, 1.0 };
	for (long sample = 0; sample < static_cast<long>(0.1 * sampleRate); ++sample) juce::ignoreUnused(svf.process(noise(random), settings));
	const auto window = static_cast<long>(0.05 * sampleRate);
	double firstPeak {}, lastPeak {};
	const auto tail = static_cast<long>(7.5 * sampleRate);
	for (long sample = 0; sample < tail; ++sample)
	{
		const auto out = svf.process(0.0, settings);
		const auto level = std::max({ std::abs(out.lowPass), std::abs(out.bandPass), std::abs(out.highPass) });
		if (sample < window) firstPeak = std::max(firstPeak, level);
		if (sample >= tail - window) lastPeak = std::max(lastPeak, level);
	}
	REQUIRE(firstPeak > 0.0);
	CHECK(lastPeak < 0.1 * firstPeak);
	CHECK(svf.diagnostics().unconvergedSamples == 0);
	CHECK(svf.diagnostics().nonFiniteSamples == 0);
}

TEST_CASE("Mono nonlinear SVF stays periodic in the cases that broke earlier topologies", "[mono][filter][svf][svf-nonlinear][svf-periodicity]")
{
	// ADR 0006's periodicity invariant, on the hostile cases the full sweep found: a saw or square with its 2nd or
	// 3rd harmonic on the cutoff at relative levels 0.5-1.4 (saturating storage, Q 2.5-8), and a saw at the cutoff
	// driven to about 2 (the first topology, from Q 2). Levels are 2 dB apart, the width of those windows.
	for (const auto q : { 2.5, 4.0, 1.0 / vekt::mono::svfDamping(1.0) })
		for (const auto [ratio, wave] : { std::pair { 1.0 / 3.0, Wave::saw }, std::pair { 0.5, Wave::saw },
			std::pair { 1.0 / 3.0, Wave::square }, std::pair { 1.0, Wave::saw } })
			for (const auto level : { 0.5, 0.63, 0.79, 1.0, 1.26, 1.58, 2.0 })
			{
				INFO("Q " << q << ", f/fc " << ratio << ", wave " << static_cast<int>(wave) << ", level " << level);
				CHECK_FALSE(persistentlyNonPeriodic(48'000.0, 1'000.0, q, ratio, wave, level));
			}
}

TEST_CASE("Mono nonlinear SVF output stays periodic with periodic input across the level sweep", "[.][svf-periodicity-full]")
{
	// The full sweep: attractors show up in windows about 2 dB wide, so the level relative to the knee is swept in
	// 1/24-decade steps from 0.05 to about 16 (past full input saturation), not in 6 dB Drive steps.
	for (const auto sampleRate : { 48'000.0, 192'000.0 })
		for (const auto q : { 2.0, 4.0, 8.0, 12.0, 16.0, 1.0 / vekt::mono::svfDamping(1.0) })
			for (const auto ratio : { 1.0 / 3.0, 0.5, 1.0, 2.0 })
				for (const auto wave : { Wave::sine, Wave::saw, Wave::square })
					for (int step = 0; step < 60; ++step)
					{
						const auto level = 0.05 * std::pow(10.0, step / 24.0);
						INFO("rate " << sampleRate << ", Q " << q << ", f/fc " << ratio << ", wave " << static_cast<int>(wave) << ", level " << level);
						CHECK_FALSE(persistentlyNonPeriodic(sampleRate, 1'000.0, q, ratio, wave, level));
					}
}

namespace
{
// Fundamental gain re the undriven input and the energy share of harmonics 2..10, for a sine through the SVF with a
// given damping curve (0: saturated input into a linear SVF, the reference) after it settles.
struct SineMeasurement
{
	double gain {}, harmonicShare {};
};

enum class SvfTap { lowPass, highPass, notch };

SineMeasurement measureSine(SvfTap tap, double frequency, double amplitude, double cutoff, double k, double driveDb, double curve)
{
	constexpr double sampleRate = 96'000.0;
	NonlinearTptSvf svf;
	svf.prepare(sampleRate);
	const auto periods = std::max(4.0, std::round(0.2 * frequency));
	const auto window = static_cast<int>(std::round(periods * sampleRate / frequency));
	const auto settle = static_cast<int>(0.5 * sampleRate);
	std::array<double, 11> inPhase {}, quadrature {};
	for (int sample = 0; sample < settle + window; ++sample)
	{
		const auto phase = 2.0 * std::numbers::pi * frequency * sample / sampleRate;
		const auto out = svf.process(amplitude * std::sin(phase), { cutoff, k, driveDb, vekt::mono::svfKnee, curve });
		const auto output = tap == SvfTap::lowPass ? out.lowPass : tap == SvfTap::highPass ? out.highPass : out.notch();
		if (sample < settle) continue;
		for (std::size_t harmonic = 1; harmonic < inPhase.size(); ++harmonic)
		{
			inPhase[harmonic] += output * std::sin(static_cast<double>(harmonic) * phase);
			quadrature[harmonic] += output * std::cos(static_cast<double>(harmonic) * phase);
		}
	}
	std::array<double, 11> level {};
	for (std::size_t harmonic = 1; harmonic < level.size(); ++harmonic)
		level[harmonic] = 2.0 * std::hypot(inPhase[harmonic], quadrature[harmonic]) / window;
	double harmonics {};
	for (std::size_t harmonic = 2; harmonic < level.size(); ++harmonic) harmonics += level[harmonic] * level[harmonic];
	return { level[1] / amplitude, harmonics / (harmonics + level[1] * level[1]) };
}

std::string decibels(double gain)
{
	return gain <= 0.0 ? std::string("-inf") : juce::String(20.0 * std::log10(gain), 1).toStdString();
}
}

// Development characterization, hidden from normal runs: prints how the SVF responds to Drive for each damping curve
// (ADR 0006). Gains are dB re the undriven input; linear expectations in brackets. Curve 0 is the saturated-input
// reference.
TEST_CASE("Mono nonlinear SVF Drive characterization", "[.][svf-characterize]")
{
	const auto qMax = vekt::mono::svfDamping(1.0);
	for (const auto curve : { 0.0, 0.25, 0.5, 1.0 })
		for (const auto amplitude : { 0.5, 1.0 })
		{
			std::cout << "\nbeta " << curve << ", input amplitude " << amplitude << "\n"
				<< "drive | HP@50Hz [-52] | Notch@fc Q2 [-inf] | Notch@fc Qmax | LP peak@fc Qmax [+18] | HP@8kHz [0] | LP@100Hz gain/THD%\n";
			for (const auto drive : { 0.0, 6.0, 12.0, 18.0, 24.0 })
			{
				const auto low = measureSine(SvfTap::lowPass, 100.0, amplitude, 1'000.0, 0.5, drive, curve);
				std::cout << "+" << drive << " dB | "
					<< decibels(measureSine(SvfTap::highPass, 50.0, amplitude, 1'000.0, 0.5, drive, curve).gain) << " | "
					<< decibels(measureSine(SvfTap::notch, 1'000.0, amplitude, 1'000.0, 0.5, drive, curve).gain) << " | "
					<< decibels(measureSine(SvfTap::notch, 1'000.0, amplitude, 1'000.0, qMax, drive, curve).gain) << " | "
					<< decibels(measureSine(SvfTap::lowPass, 1'000.0, amplitude, 1'000.0, qMax, drive, curve).gain) << " | "
					<< decibels(measureSine(SvfTap::highPass, 8'000.0, amplitude, 1'000.0, 0.5, drive, curve).gain) << " | "
					<< decibels(low.gain) << " / " << juce::String(100.0 * std::sqrt(low.harmonicShare), 1) << "\n";
			}
		}
}

// Development characterization, hidden from normal runs: resonance prominence, the LP fundamental at the cutoff
// over the LP passband fundamental at the same Drive, for each damping curve (ADR 0006 voicing). Gains are also given
// re the fundamental of the saturated drive u = a tanh(D x / a), which removes Drive gain and input compression.
TEST_CASE("Mono nonlinear SVF resonance prominence", "[.][svf-prominence]")
{
	constexpr double amplitude = 1.0;
	constexpr double cutoff = 1'000.0;
	// Fundamental of u for a unit-frequency sine of the given amplitude.
	const auto driveFundamental = [&](double driveDb)
	{
		const auto drive = std::pow(10.0, driveDb / 20.0);
		constexpr int points = 4'096;
		double inPhase {};
		for (int point = 0; point < points; ++point)
		{
			const auto phase = 2.0 * std::numbers::pi * point / points;
			inPhase += vekt::mono::nonlinearTptSvfSaturate(drive * amplitude * std::sin(phase), vekt::mono::svfKnee) * std::sin(phase);
		}
		return 2.0 * inPhase / points / amplitude;
	};
	for (const auto resonance : { 0.9, 1.0 })
	{
		const auto k = vekt::mono::svfDamping(resonance);
		std::cout << "\nResonance " << 100.0 * resonance << " % (Q " << juce::String(1.0 / k, 1) << ", linear prominence "
			<< decibels(1.0 / k) << " dB), input level " << amplitude << "\n"
			<< "beta | drive | u gain | passband@100Hz re u / THD% | peak@fc re u / THD% | prominence\n";
		for (const auto curve : { 0.0, 0.25, 0.5, 1.0 })
			for (const auto drive : { 0.0, 6.0, 12.0, 18.0, 24.0 })
			{
				const auto u = driveFundamental(drive);
				const auto passband = measureSine(SvfTap::lowPass, 100.0, amplitude, cutoff, k, drive, curve);
				const auto peak = measureSine(SvfTap::lowPass, cutoff, amplitude, cutoff, k, drive, curve);
				std::cout << curve << " | +" << drive << " dB | " << decibels(u) << " | "
					<< decibels(passband.gain / u) << " / " << juce::String(100.0 * std::sqrt(passband.harmonicShare), 1) << " | "
					<< decibels(peak.gain / u) << " / " << juce::String(100.0 * std::sqrt(peak.harmonicShare), 1) << " | "
					<< decibels(peak.gain / passband.gain) << "\n";
			}
	}
}

// Development audition, hidden: candidate SVF maximum Q (ADR 0006). The Resonance map keeps the 29 September law to 90 %
// and smoothsteps k from 0.125 to 1 / maximumQ over the top 10 %, as svfDamping does with svfMaximumQ; maximum Q 8 is
// the earlier map. With VEKT_MONO_DUMP set,
// writes 96 kHz mono WAVs to svf-q/fixed-gain and svf-q/rms-matched: an A2 saw at mixer level 1 through the
// filter-envelope sweep (150 Hz -> 6 kHz -> 150 Hz) at Resonance 90 / 95 / 100 % and Drive 0 / +12 / +24 dB, the
// Ladder at the same settings for reference, and plucks at Resonance 100 % (0.15 s notes every 0.6 s): with the
// cutoff fixed between harmonics (600 Hz) and on the 6th (660 Hz), where Q decides the ring, and with a filter-envelope
// pluck (cutoff 4 kHz falling to 150 Hz, time constant 0.1 s), each at Drive 0 and +12 dB. SVF output is trimmed as in
// the voice (svfOutputTrim); the Ladder is raw.
TEST_CASE("Mono SVF maximum Q audition renders", "[.][svf-q-renders]")
{
	using namespace vekt::test::filter_prototype;
	const auto* directory = std::getenv("VEKT_MONO_DUMP");
	REQUIRE(directory != nullptr);
	const auto folder = juce::File(directory).getChildFile("svf-q");
	const auto fixedFolder = folder.getChildFile("fixed-gain"), matchedFolder = folder.getChildFile("rms-matched");
	for (const auto& old : { fixedFolder, matchedFolder })
		for (const auto& file : old.findChildFiles(juce::File::findFiles, false, "*.wav")) file.deleteFile();
	REQUIRE(fixedFolder.createDirectory().wasOk());
	REQUIRE(matchedFolder.createDirectory().wasOk());
	constexpr double sampleRate = 96'000.0, note = 110.0;
	const auto damping = [](double resonance, double maximumQ)
	{
		const auto r = std::clamp(resonance, 0.0, 1.0);
		const auto t = std::clamp((r - 0.9) / 0.1, 0.0, 1.0);
		return 2.0 - 1.875 * std::pow(r, vekt::mono::svfResonanceShape) - (0.125 - 1.0 / maximumQ) * t * t * (3.0 - 2.0 * t);
	};
	// At the shipping maximum this is the production map.
	for (int step = 0; step <= 100; ++step)
		REQUIRE(std::abs(damping(step / 100.0, vekt::mono::svfMaximumQ) - vekt::mono::svfDamping(step / 100.0)) < 1.0e-15);
	const auto sweepCutoff = [](double t)
	{
		const auto position = t < 2.0 ? t / 2.0 : std::max(0.0, 1.0 - (t - 2.0) / 2.0);
		return 150.0 * std::pow(40.0, position);
	};
	enum class Kind { svfSweep, ladderSweep, svfPluck, ladderPluck };
	struct Render
	{
		std::string name;
		Kind kind {};
		double resonance {}, driveDb {}, maximumQ {};
		double pluckCutoff {}; // fixed pluck cutoff in Hz; 0 for the filter-envelope pluck
	};
	const auto percent = [](double value) { return std::to_string(static_cast<int>(std::lround(value * 100.0))); };
	const auto whole = [](double value) { return std::to_string(static_cast<int>(value)); };
	std::vector<Render> renders;
	for (const auto resonance : { 0.9, 0.95, 1.0 })
		for (const auto db : { 0.0, 12.0, 24.0 })
		{
			// At 90 % every candidate is the current map: render it once.
			for (const auto q : resonance <= 0.9 ? std::vector { 8.0 } : std::vector { 8.0, 12.0, 16.0, 20.0 })
				renders.push_back({ "svf-q" + whole(q) + "-sweep-res" + percent(resonance) + "-drive" + whole(db), Kind::svfSweep, resonance, db, q });
			renders.push_back({ "ladder-sweep-res" + percent(resonance) + "-drive" + whole(db), Kind::ladderSweep, resonance, db, 0.0 });
		}
	for (const auto& [pluckName, pluckCutoff] : { std::pair { std::string("between"), 600.0 }, std::pair { std::string("onharmonic"), 660.0 },
		std::pair { std::string("envelope"), 0.0 } })
		for (const auto db : { 0.0, 12.0 })
		{
			const auto suffix = "-pluck-" + pluckName + "-res100-drive" + whole(db);
			for (const auto q : { 8.0, 12.0, 16.0, 20.0 })
				renders.push_back({ "svf-q" + whole(q) + suffix, Kind::svfPluck, 1.0, db, q, pluckCutoff });
			renders.push_back({ "ladder" + suffix, Kind::ladderPluck, 1.0, db, 0.0, pluckCutoff });
		}
	const auto outputs = parallelMap(renders.size(), [&](std::size_t index)
	{
		const auto& render = renders[index];
		const auto pluck = render.kind == Kind::svfPluck || render.kind == Kind::ladderPluck;
		const auto ladder = render.kind == Kind::ladderSweep || render.kind == Kind::ladderPluck;
		AdditiveSaw saw { note, sampleRate };
		NonlinearTptSvf svf;
		vekt::mono::NonlinearTptLadder ladderFilter;
		svf.prepare(sampleRate);
		ladderFilter.prepare(sampleRate);
		NonlinearTptSvfSettings svfSettings { render.pluckCutoff > 0.0 ? render.pluckCutoff : 600.0, ladder ? 2.0 : damping(render.resonance, render.maximumQ), render.driveDb,
			vekt::mono::svfKnee, vekt::mono::svfDampingCurve };
		vekt::mono::NonlinearTptLadderSettings ladderSettings { static_cast<float>(svfSettings.cutoffHz), static_cast<float>(render.resonance),
			static_cast<float>(render.driveDb) };
		const auto trim = vekt::mono::svfOutputTrim(render.resonance);
		const auto length = static_cast<long>((pluck ? 4.8 : 4.5) * sampleRate);
		std::vector<float> out;
		out.reserve(static_cast<std::size_t>(length));
		for (long sample = 0; sample < length; ++sample)
		{
			const auto time = static_cast<double>(sample) / sampleRate;
			auto x = saw.next();
			if (pluck)
			{
				// 0.15 s notes every 0.6 s with 2 ms edges, so the tails are the filter's, not a click.
				const auto position = std::fmod(time, 0.6);
				x *= std::clamp(std::min(position, 0.15 - position) / 0.002, 0.0, 1.0);
				if (render.pluckCutoff <= 0.0)
				{
					svfSettings.cutoffHz = 150.0 + 3'850.0 * std::exp(-position / 0.1);
					ladderSettings.cutoffHz = static_cast<float>(svfSettings.cutoffHz);
				}
			}
			else
			{
				svfSettings.cutoffHz = sweepCutoff(time);
				ladderSettings.cutoffHz = static_cast<float>(svfSettings.cutoffHz);
			}
			const auto y = ladder ? static_cast<double>(ladderFilter.processCoupled(static_cast<float>(x), ladderSettings))
				: trim * svf.process(x, svfSettings).lowPass;
			out.push_back(static_cast<float>(y));
		}
		CHECK(svf.diagnostics().unconvergedSamples == 0);
		CHECK(svf.diagnostics().nonFiniteSamples == 0);
		CHECK(ladderFilter.diagnostics().unconvergedSamples == 0);
		return out;
	});
	double peak {};
	for (const auto& out : outputs)
		for (const auto sample : out) peak = std::max(peak, static_cast<double>(std::abs(sample)));
	REQUIRE(peak > 0.0);
	const auto fixedGain = std::pow(10.0, -1.0 / 20.0) / peak;
	std::cout << "\nfixed gain " << dB(fixedGain) << " dB; file | RMS / peak in the fixed-gain set (dBFS)\n";
	for (std::size_t index = 0; index < renders.size(); ++index)
	{
		INFO(renders[index].name);
		REQUIRE(rmsOf(outputs[index]) > 0.0);
		writeWav(fixedFolder.getChildFile(renders[index].name + ".wav"), outputs[index], fixedGain, sampleRate);
		writeWav(matchedFolder.getChildFile(renders[index].name + ".wav"), outputs[index], 0.1 / rmsOf(outputs[index]), sampleRate);
		double renderPeak {};
		for (const auto sample : outputs[index]) renderPeak = std::max(renderPeak, static_cast<double>(std::abs(sample)));
		std::cout << renders[index].name << " | " << dB(fixedGain * rmsOf(outputs[index])) << " / " << dB(fixedGain * renderPeak) << "\n";
	}
}
