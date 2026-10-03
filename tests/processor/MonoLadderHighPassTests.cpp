#include <vekt/dsp/OversamplingQuality.h>
#include "FilterPrototypeSupport.h"
#include "LadderPoleMix.h"
#include "LadderResonance.h"
#include "NonlinearTptLadder.h"
#include "NonlinearTptLadderHighPass.h"
#include "NonlinearTptSvf.h"
#include "SvfResponse.h"

#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cmath>
#include <complex>
#include <iostream>
#include <numbers>
#include <random>
#include <string>
#include <vector>

// The Ladder's high-pass ladder (NonlinearTptLadderHighPass.h, ADR 0009).

namespace
{
using vekt::mono::NonlinearTptLadderHighPass;
using vekt::mono::NonlinearTptLadderHighPassSettings;

constexpr std::array hostRates { 44'100.0, 48'000.0, 96'000.0, 192'000.0 };

NonlinearTptLadderHighPassSettings highPassSettings(double cutoff, double feedback, double driveDb = 0.0, bool linear = false)
{
	NonlinearTptLadderHighPassSettings settings;
	settings.cutoffHz = cutoff;
	settings.feedback = feedback;
	settings.driveDecibels = driveDb;
	settings.linear = linear;
	return settings;
}

// The bilinear (prewarped at the cutoff) HP^4 / (1 + k HP^4) at frequency.
double analyticGain(double frequency, double cutoff, double feedback, double sampleRate)
{
	const auto omega = std::tan(std::numbers::pi * frequency / sampleRate) / std::tan(std::numbers::pi * cutoff / sampleRate);
	const std::complex<double> s { 0.0, omega };
	const auto hp4 = std::pow(s / (1.0 + s), 4);
	return std::abs(hp4 / (1.0 + feedback * hp4));
}

double measuredGain(double frequency, double cutoff, double feedback, double sampleRate, double amplitude, bool linear)
{
	NonlinearTptLadderHighPass filter;
	filter.prepare(sampleRate);
	const auto settings = highPassSettings(cutoff, feedback, 0.0, linear);
	// The slowest decay measured here (k = 3.6) has a time constant of about 6 ms: 0.1 s settles it below 1e-7. The window
	// holds whole periods, so the projection needs no longer one.
	const auto settle = static_cast<long>(0.1 * sampleRate);
	const auto periods = std::max(8.0, std::floor(0.05 * frequency));
	const auto window = static_cast<long>(std::lround(periods * sampleRate / frequency));
	double inPhase {}, quadrature {};
	for (long sample = 0; sample < settle + window; ++sample)
	{
		const auto phase = 2.0 * std::numbers::pi * frequency * static_cast<double>(sample) / sampleRate;
		const auto out = filter.process(amplitude * std::sin(phase), settings);
		if (sample < settle) continue;
		inPhase += out * std::sin(phase);
		quadrature += out * std::cos(phase);
	}
	return 2.0 * std::hypot(inPhase, quadrature) / static_cast<double>(window) / amplitude;
}

// ITU-R BS.1770 K-weighting at 48 kHz.
struct KWeighting
{
	struct Biquad
	{
		double b0, b1, b2, a1, a2, x1 {}, x2 {}, y1 {}, y2 {};
		double process(double x) noexcept
		{
			const auto y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
			x2 = x1;
			x1 = x;
			y2 = y1;
			y1 = y;
			return y;
		}
	};
	Biquad shelf { 1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241, 0.73248077421585 };
	Biquad highPass { 1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621 };
	double process(double x) noexcept { return highPass.process(shelf.process(x)); }
};
}

TEST_CASE("Mono high-pass ladder is the low-pass ladder's mirror at small signals", "[mono][filter][ladder-hp]")
{
	// Exact for the linear reference; within 0.05 dB at 1e-4 for the non-linear filter. The rate enters only through the
	// prewarped tan(pi fc / fs), so the lowest internal rate, 48 kHz and the highest (192 kHz at 16x) cover it.
	for (const auto sampleRate : { 44'100.0, 48'000.0, vekt::dsp::maximumInternalSampleRate })
		for (const auto feedback : { 0.0, 2.0, 3.6 })
			for (const auto ratio : { 0.125, 0.5, 1.0, 2.0, 8.0 })
			{
				const auto cutoff = 1'000.0, frequency = ratio * cutoff;
				const auto expected = 20.0 * std::log10(analyticGain(frequency, cutoff, feedback, sampleRate));
				INFO("rate " << sampleRate << ", k " << feedback << ", f / fc " << ratio << ": expected " << expected << " dB");
				CHECK(std::abs(20.0 * std::log10(measuredGain(frequency, cutoff, feedback, sampleRate, 1.0, true)) - expected) < 0.01);
				CHECK(std::abs(20.0 * std::log10(measuredGain(frequency, cutoff, feedback, sampleRate, 1.0e-4, false)) - expected) < 0.05);
			}
}

TEST_CASE("Mono high-pass ladder has no solver failures under hostile rendering", "[mono][filter][ladder-hp]")
{
	for (const auto hostRate : hostRates)
		for (const auto factor : { 1, 8 })
			for (const auto feedback : { 0.0, 2.0, 3.92, 4.6 })
				for (const auto driveDb : { 0.0, 24.0 })
				{
					const auto sampleRate = hostRate * factor;
					NonlinearTptLadderHighPass filter;
					filter.prepare(sampleRate);
					auto settings = highPassSettings(1'000.0, feedback, driveDb);
					std::mt19937 random { 71 };
					std::uniform_real_distribution noise { -0.3, 0.3 };
					double peak {};
					const auto total = static_cast<long>(0.05 * sampleRate);
					const auto period = static_cast<int>(std::lround(sampleRate / 110.0));
					for (long sample = 0; sample < total; ++sample)
					{
						settings.cutoffHz = 1'000.0 * std::exp2(4.0 * std::sin(2.0 * std::numbers::pi * 7.0 * static_cast<double>(sample) / sampleRate));
						const auto phase = static_cast<double>(sample % period) / period;
						const auto input = sample < total * 2 / 3 ? 2.0 * (2.0 * phase - 1.0) + noise(random) : 0.0;
						peak = std::max(peak, std::abs(filter.process(input, settings)));
					}
					INFO("rate " << hostRate << " x" << factor << ", k " << feedback << ", Drive " << driveDb << ", max iterations "
						<< filter.diagnostics().maximumIterations);
					CHECK(filter.diagnostics().nonFiniteSamples == 0);
					CHECK(filter.diagnostics().unconvergedSamples == 0);
					CHECK(filter.diagnostics().maximumIterations < 60);
					CHECK(std::isfinite(peak));
				}
}

TEST_CASE("Mono high-pass ladder feedback stops short of self-oscillation and keeps the knob alive", "[mono][filter][ladder-hp]")
{
	using vekt::mono::ladderHighPassFeedback;
	for (int step = 0; step <= 90; ++step)
		REQUIRE(std::bit_cast<std::uint64_t>(ladderHighPassFeedback(step / 100.0)) == std::bit_cast<std::uint64_t>(vekt::mono::ladderFeedbackGain(step / 100.0)));
	// 100 % is the low-pass ladder's 97.9 %: k = 3.916, below the onset at 4; still rising, with no slope step at 90 %.
	REQUIRE(std::abs(ladderHighPassFeedback(1.0) - vekt::mono::ladderFeedbackGain(0.979)) < 1.0e-12);
	REQUIRE(ladderHighPassFeedback(1.0) < 3.95);
	auto previous = ladderHighPassFeedback(0.9);
	for (int step = 1; step <= 100; ++step)
	{
		const auto value = ladderHighPassFeedback(0.9 + 0.001 * step);
		REQUIRE(value > previous);
		previous = value;
	}
	const auto below = (ladderHighPassFeedback(0.9) - ladderHighPassFeedback(0.9 - 1.0e-6)) / 1.0e-6;
	const auto above = (ladderHighPassFeedback(0.9 + 1.0e-6) - ladderHighPassFeedback(0.9)) / 1.0e-6;
	REQUIRE(std::abs(above - below) < 1.0e-3 * below);
}

TEST_CASE("Mono high-pass ladder decays below threshold and self-oscillates at the cutoff above it", "[mono][filter][ladder-hp]")
{
	// Stopped short in the voice (ladderHighPassFeedback), but the filter itself: silence after excitation through
	// k = 3.916 (the HP's 100 %), and an oscillation exactly at the cutoff for k > 4 (linear stages, memoryless tanh).
	constexpr double sampleRate = 48'000.0, cutoff = 1'000.0;
	for (const auto feedback : { vekt::mono::ladderHighPassFeedback(1.0), 4.3 })
	{
		NonlinearTptLadderHighPass filter;
		filter.prepare(sampleRate);
		const auto settings = highPassSettings(cutoff, feedback);
		std::mt19937 random { 73 };
		std::uniform_real_distribution noise { -0.5, 0.5 };
		std::vector<double> tail;
		for (long sample = 0; sample < static_cast<long>(2.05 * sampleRate); ++sample)
		{
			const auto out = filter.process(sample < static_cast<long>(0.05 * sampleRate) ? noise(random) : 0.0, settings);
			if (sample >= static_cast<long>(1.55 * sampleRate)) tail.push_back(out);
		}
		double energy {};
		for (const auto value : tail) energy += value * value;
		const auto rms = std::sqrt(energy / static_cast<double>(tail.size()));
		INFO("k " << feedback << ": final RMS " << rms);
		if (feedback < 4.0) CHECK(rms < 1.0e-6);
		else
		{
			REQUIRE(rms > 0.1);
			CHECK(std::abs(vekt::test::filter_prototype::analyseCycles(tail, sampleRate).frequency / cutoff - 1.0) < 0.002);
		}
	}
}

// Development measurement, hidden (Phase 2): decay, onset and self-oscillation pitch; non-linear residue and level
// stability on a Classic Three Bass-like mix against the current tap-mix high-pass (raw and with the baseline's input law)
// and the SVF; switching level against the SVF without any lift.
TEST_CASE("Mono high-pass ladder measurements", "[.][ladder-hp-prototype]")
{
	using namespace vekt::test::filter_prototype;
	constexpr double sampleRate = 48'000.0;
	// Classic Three Bass-like mix: a 65.4 Hz sine at 1, a 130.6 Hz sine at 0.72, a 131.1 Hz band-limited saw at 0.54.
	enum class Kind { highPassLadder, tapRaw, svf };
	constexpr std::array kindNames { "HP ladder", "tap HP raw", "SVF HP" };
	struct Job { Kind kind; double cutoff, resonance, driveDb; };
	std::vector<Job> jobs;
	for (const auto kind : { Kind::highPassLadder, Kind::tapRaw, Kind::svf })
		for (const auto driveDb : { 0.0, 12.0, 24.0 })
			for (const auto cutoff : { 1'000.0, 3'000.0 })
				for (const auto resonance : { 0.0, 0.5, 0.9, 0.97, 1.0 }) jobs.push_back({ kind, cutoff, resonance, driveDb });
	const auto render = [](const Job& job, double scale)
	{
		NonlinearTptLadderHighPass highPass;
		vekt::mono::NonlinearTptLadder ladder;
		vekt::mono::NonlinearTptSvf svf;
		highPass.prepare(sampleRate);
		ladder.prepare(sampleRate);
		svf.prepare(sampleRate);
		AdditiveSaw saw { 131.1, sampleRate };
		const auto highPassSetting = highPassSettings(job.cutoff, vekt::mono::ladderHighPassFeedback(job.resonance), job.driveDb);
		const vekt::mono::NonlinearTptLadderSettings ladderSettings { static_cast<float>(job.cutoff), static_cast<float>(job.resonance),
			static_cast<float>(job.driveDb), false, 0.0f, 1.0f };
		const vekt::mono::NonlinearTptSvfSettings svfSettings { job.cutoff, vekt::mono::svfDamping(job.resonance), job.driveDb, vekt::mono::svfKnee,
			vekt::mono::svfDampingCurve };
		std::vector<double> y;
		for (long sample = 0; sample < static_cast<long>(2.5 * sampleRate); ++sample)
		{
			const auto t = static_cast<double>(sample) / sampleRate;
			const auto x = scale * (std::sin(2.0 * std::numbers::pi * 65.4 * t) + 0.72 * std::sin(2.0 * std::numbers::pi * 130.6 * t) + 0.54 * saw.next());
			const auto out = job.kind == Kind::highPassLadder ? highPass.process(x, highPassSetting)
				: job.kind == Kind::tapRaw ? static_cast<double>(ladder.processCoupled(static_cast<float>(x), ladderSettings))
				: svf.process(x, svfSettings).highPass;
			if (sample >= static_cast<long>(0.5 * sampleRate)) y.push_back(out / scale);
		}
		return y;
	};
	struct Result { double residueDb, rangeDb, linearRangeDb; };
	const auto range = [](const std::vector<double>& y)
	{
		constexpr std::size_t window = 2'400;
		double low = std::numeric_limits<double>::infinity(), high = -low;
		for (std::size_t start = 0; start + window <= y.size(); start += window)
		{
			double energy {};
			for (std::size_t sample = start; sample < start + window; ++sample) energy += y[sample] * y[sample];
			low = std::min(low, 10.0 * std::log10(energy / window + 1.0e-30));
			high = std::max(high, 10.0 * std::log10(energy / window + 1.0e-30));
		}
		return high - low;
	};
	const auto results = parallelMap(jobs.size(), [&](std::size_t index)
	{
		const auto nonlinear = render(jobs[index], 1.0), linear = render(jobs[index], 1.0e-2);
		double residue {}, energy {};
		for (std::size_t sample = 0; sample < nonlinear.size(); ++sample)
		{
			residue += (nonlinear[sample] - linear[sample]) * (nonlinear[sample] - linear[sample]);
			energy += linear[sample] * linear[sample];
		}
		return Result { 10.0 * std::log10(residue / energy), range(nonlinear), range(linear) };
	});
	std::cout << "\nDetuned mix: non-linear residue re the linear output / 50 ms level range / the linear reference's range, dB\n";
	for (std::size_t index = 0; index < jobs.size(); ++index)
	{
		const auto& job = jobs[index];
		std::cout << kindNames[static_cast<std::size_t>(job.kind)] << ", Drive +" << job.driveDb << ", cutoff " << job.cutoff << ", Res "
			<< 100.0 * job.resonance << " %: " << juce::String(results[index].residueDb, 1) << " / " << juce::String(results[index].rangeDb, 2)
			<< " / " << juce::String(results[index].linearRangeDb, 2) << "\n";
	}
}

TEST_CASE("Mono high-pass ladder primes a restart on a running signal", "[mono][filter][ladder-hp]")
{
	// MonoVoice rests the high-pass ladder after a second at unmodulated LP and primes it as Mode leaves: from the steady
	// state for the current (saturated) input, it sees no step. Against a filter that ran all along, a cold restart on a
	// saw differs by -12 to -17 dB re the signal in its loudest 5 ms; primed, -37 to -53 dB from a 1 kHz cutoff, and at
	// 250 Hz -30 / -15 dB (55 / 220 Hz saw). With Resonance the remaining difference is the filter's own ring at the
	// cutoff, which is why the voice never rests it while an LFO reaches Mode (ADR 0009). Values: run with -s.
	constexpr double sampleRate = 48'000.0;
	const auto loudestDifference = [](double frequency, double cutoff, bool primed)
	{
		const auto settings = highPassSettings(cutoff, 0.0);
		NonlinearTptLadderHighPass warm, cold;
		warm.prepare(sampleRate);
		cold.prepare(sampleRate);
		constexpr int start = 24'077, length = 24'000, window = 240;
		std::vector<double> warmOutput, difference;
		double phase {};
		for (int sample = 0; sample < start + length; ++sample)
		{
			const auto saw = 2.0 * phase - 1.0;
			phase += frequency / sampleRate;
			phase -= std::floor(phase);
			const auto w = warm.process(saw, settings);
			if (sample < start) continue;
			if (sample == start && primed) cold.prime(saw, settings);
			warmOutput.push_back(w);
			difference.push_back(cold.process(saw, settings) - w);
		}
		double signal {}, loudest {};
		for (const auto value : warmOutput) signal += value * value;
		for (std::size_t first = 0; first + window <= difference.size(); first += window)
		{
			double energy {};
			for (auto sample = first; sample < first + window; ++sample) energy += difference[sample] * difference[sample];
			loudest = std::max(loudest, energy / window);
		}
		return 10.0 * std::log10(loudest / (signal / length) + 1.0e-30);
	};
	for (const auto frequency : { 55.0, 220.0 })
		for (const auto cutoff : { 250.0, 1'000.0, 4'000.0 })
		{
			const auto reset = loudestDifference(frequency, cutoff, false), primed = loudestDifference(frequency, cutoff, true);
			INFO("saw " << frequency << " Hz, cutoff " << cutoff << ": reset " << reset << " dB, primed " << primed << " dB re the signal");
			CHECK(primed < reset - 3.0);
			if (cutoff >= 1'000.0) CHECK(primed < -30.0);
		}
}

TEST_CASE("Mono high-pass ladder primes to the exact steady state", "[mono][filter][ladder-hp]")
{
	// Primed on a constant input, the filter is already settled, at full Drive and the top feedback too: silent from the
	// first sample, and the solve converges at its first guess. Unprimed, the same input is a step.
	for (const auto driveDb : { 0.0, 24.0 })
		for (const auto feedback : { 0.0, vekt::mono::ladderHighPassFeedback(1.0) })
			for (const auto input : { -0.8, 0.3, 1.7 })
			{
				const auto settings = highPassSettings(1'000.0, feedback, driveDb);
				NonlinearTptLadderHighPass primed, cold;
				primed.prepare(48'000.0);
				cold.prepare(48'000.0);
				primed.prime(input, settings);
				double loudest {};
				for (int sample = 0; sample < 4'800; ++sample) loudest = std::max(loudest, std::abs(primed.process(input, settings)));
				INFO("Drive " << driveDb << " dB, k " << feedback << ", input " << input << ": loudest " << loudest);
				CHECK(loudest < 1.0e-12);
				CHECK(primed.diagnostics().iterations == 0);
				CHECK(std::abs(cold.process(input, settings)) > 0.01);
			}
}
