#include <vekt/dsp/LinearTptSvf.h>

#include "FilterPrototypeSupport.h"
#include "Korg35Response.h"
#include "LadderResonance.h"
#include "NonlinearTptKorg35.h"
#include "NonlinearTptLadder.h"
#include "NonlinearTptLadderHighPass.h"
#include "NonlinearTptSvf.h"
#include "SvfResponse.h"


#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <complex>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <numbers>
#include <random>
#include <string>
#include <vector>

namespace
{
using namespace vekt::test::filter_prototype;
using vekt::dsp::LinearTptSvf;
using vekt::kobber::NonlinearTptKorg35;
using vekt::kobber::NonlinearTptKorg35Settings;
using vekt::kobber::nonlinearTptKorg35Bracket;
using vekt::kobber::nonlinearTptKorg35LinearDifference;
using vekt::kobber::nonlinearTptKorg35MaximumFeedback;
using vekt::kobber::nonlinearTptKorg35MaximumIterations;
using vekt::kobber::nonlinearTptKorg35Residual;
using vekt::kobber::nonlinearTptKorg35Threshold;
using vekt::kobber::solveNonlinearTptKorg35;

constexpr std::array hostRates { 44'100.0, 48'000.0, 96'000.0, 192'000.0 };
constexpr std::array oversamplingFactors { 1, 2, 4, 8 };

NonlinearTptKorg35Settings settingsFor(double cutoff, double feedback, double knee = 1.0)
{
	NonlinearTptKorg35Settings settings;
	settings.cutoffHz = cutoff;
	settings.feedback = feedback;
	settings.knee = knee;
	return settings;
}
}

// The small-signal Korg35 is Stinchcombe's equation 8, 1 / (p^2 + (7/3 - rho) p + 1): the canonical SVF low-pass with
// k = 7/3 - rho is the oracle for the linear reference.
TEST_CASE("Kobber Korg35 linear reference matches LinearTptSvf with k = 7/3 - rho", "[kobber][filter][k35][k35-linear]")
{
	double worst {};
	for (const auto hostRate : hostRates)
		for (const auto factor : oversamplingFactors)
		{
			const auto sampleRate = hostRate * factor;
			for (const auto cutoff : { 2.5, 1'000.0, 15'000.0, 0.44 * sampleRate })
				for (const auto feedback : { 0.0, 0.5, 1.5, 2.2, 2.3 })
					for (const auto driveDb : { 0.0, 12.0 })
					{
						LinearTptSvf svf;
						NonlinearTptKorg35 filter;
						svf.prepare(sampleRate);
						filter.prepare(sampleRate);
						auto settings = settingsFor(cutoff, feedback);
						settings.driveDecibels = driveDb;
						settings.linear = true;
						const auto drive = std::pow(10.0, driveDb / 20.0);
						std::mt19937 random { 61 };
						std::uniform_real_distribution noise { -1.0, 1.0 };
						double peak {}, error {};
						for (int sample = 0; sample < 4'096; ++sample)
						{
							const auto input = noise(random);
							const auto reference = svf.process(drive * input, cutoff, nonlinearTptKorg35Threshold - feedback).lowPass;
							peak = std::max(peak, std::abs(reference));
							error = std::max(error, std::abs(filter.process(input, settings) - reference));
						}
						INFO("rate " << hostRate << " x" << factor << ", cutoff " << cutoff << ", rho " << feedback << ", drive " << driveDb);
						REQUIRE(peak > 0.0);
						worst = std::max(worst, error / peak);
						CHECK(error <= 1.0e-9 * peak);
					}
		}
	std::cout << "Korg35 linear reference vs LinearTptSvf: worst error " << worst << " of peak\n";
}

TEST_CASE("Kobber Korg35 converges to its linear reference at low level", "[kobber][filter][k35][k35-linear]")
{
	for (const auto hostRate : hostRates)
		for (const auto factor : oversamplingFactors)
			for (const auto feedback : { 0.0, 1.5, 2.2 })
			{
				const auto sampleRate = hostRate * factor;
				NonlinearTptKorg35 linear, nonlinear;
				linear.prepare(sampleRate);
				nonlinear.prepare(sampleRate);
				const auto settings = settingsFor(1'000.0, feedback);
				auto linearSettings = settings;
				linearSettings.linear = true;
				std::mt19937 random { 62 };
				std::uniform_real_distribution noise { -1.0e-6, 1.0e-6 };
				double peak {}, error {};
				for (int sample = 0; sample < 4'096; ++sample)
				{
					const auto input = noise(random);
					const auto reference = linear.process(input, linearSettings);
					peak = std::max(peak, std::abs(reference));
					error = std::max(error, std::abs(nonlinear.process(input, settings) - reference));
				}
				INFO("rate " << hostRate << " x" << factor << ", rho " << feedback);
				REQUIRE(peak > 0.0);
				CHECK(error <= 1.0e-6 * peak);
				CHECK(nonlinear.diagnostics().unconvergedSamples == 0);
			}
}

TEST_CASE("Kobber Korg35 diode stage has unit slope at zero and slope 1 / G beyond the knee", "[kobber][filter][k35]")
{
	const auto at = [](double input) { return vekt::kobber::nonlinearTptKorg35Stage(input, 1.0, 58.0); };
	CHECK(at(0.0).value == 0.0);
	CHECK(std::abs(at(0.0).slope - 1.0) < 1.0e-15);
	CHECK(std::abs(at(40.0).slope - 1.0 / 58.0) < 1.0e-12);
	CHECK(std::abs(at(40.0).value - (40.0 / 58.0 + 57.0 / 58.0)) < 1.0e-12);
	for (const auto z : { -5.0, -0.5, 0.2, 1.0, 3.0 })
	{
		const auto h = 1.0e-6;
		CHECK(std::abs(at(z).slope - (at(z + h).value - at(z - h).value) / (2.0 * h)) < 1.0e-7);
	}
}

namespace
{
struct Problem
{
	double s1, s2, g, feedback, driven, knee;
};

Problem randomProblem(std::mt19937& random)
{
	std::uniform_real_distribution unit { 0.0, 1.0 };
	const auto between = [&](double low, double high) { return low + (high - low) * unit(random); };
	constexpr std::array knees { 0.25, 1.0, 4.0 };
	const auto knee = knees[random() % knees.size()];
	return { between(-8.0, 8.0) * knee, between(-8.0, 8.0) * knee, std::exp(between(std::log(5.0e-6), std::log(6.4))),
		between(0.0, nonlinearTptKorg35MaximumFeedback), between(-48.0, 48.0), knee };
}

double residual(const Problem& p, double difference)
{
	return nonlinearTptKorg35Residual(difference, p.s1, p.s2, p.g, p.feedback, p.driven, p.knee, 58.0);
}

double residualScale(const Problem& p)
{
	return 1.0e-12 * (1.0 + std::abs(p.s1) + (1.0 + p.g) * std::abs(p.s2) + p.g * std::abs(p.driven) + p.feedback * p.knee);
}
}

TEST_CASE("Kobber Korg35 residual is strictly increasing with its root in the analytic bracket, and the solve converges",
	"[kobber][filter][k35][k35-solver]")
{
	std::mt19937 random { 63 };
	std::uniform_real_distribution unit { 0.0, 1.0 };
	int worstLinear {}, worstEnds {};
	for (int trial = 0; trial < 50'000; ++trial)
	{
		const auto p = randomProblem(random);
		const auto [low, high] = nonlinearTptKorg35Bracket(p.s1, p.s2, p.g, p.feedback, p.driven, p.knee, 58.0);
		INFO("s1 " << p.s1 << " s2 " << p.s2 << " g " << p.g << " rho " << p.feedback << " x " << p.driven << " K " << p.knee);
		const auto scale = residualScale(p);
		REQUIRE(residual(p, low) <= scale);
		REQUIRE(residual(p, high) >= -scale);
		// R' >= 1 + (7/3 - rho) g + g^2 > 0.
		const auto slope = 1.0 + (7.0 / 3.0 - p.feedback) * p.g + p.g * p.g;
		REQUIRE(slope > 0.0);
		const auto first = low - 1.0 + (high - low + 2.0) * unit(random), second = low - 1.0 + (high - low + 2.0) * unit(random);
		REQUIRE(residual(p, std::max(first, second)) - residual(p, std::min(first, second))
			>= slope * std::abs(first - second) - 1.0e-9 * (1.0 + p.g) * (1.0 + p.g) * (1.0 + std::abs(first) + std::abs(second) + std::abs(p.s1) + std::abs(p.s2)));
		const auto seed = nonlinearTptKorg35LinearDifference(p.s1, p.s2, p.g, p.feedback, p.driven);
		const std::array starts { seed, low, high };
		for (std::size_t index = 0; index < starts.size(); ++index)
		{
			const auto solution = solveNonlinearTptKorg35(p.s1, p.s2, p.g, p.feedback, p.driven, p.knee, 58.0, starts[index]);
			REQUIRE(solution.converged);
			REQUIRE(solution.difference >= low);
			REQUIRE(solution.difference <= high);
			REQUIRE(std::abs(residual(p, solution.difference)) <= scale);
			auto& worst = index == 0 ? worstLinear : worstEnds;
			worst = std::max(worst, solution.iterations);
		}
	}
	std::cout << "Korg35 solver: worst " << worstLinear << " iterations from the linear seed, " << worstEnds << " from bracket ends\n";
	CHECK(worstLinear < nonlinearTptKorg35MaximumIterations);
	CHECK(worstEnds < nonlinearTptKorg35MaximumIterations);
}

TEST_CASE("Kobber Korg35 has no solver failures under hostile rendering over the rate matrix", "[kobber][filter][k35][k35-solver]")
{
	// The high-pass input (Mode) only shifts the solve's second state, so the same guarantees hold at every blend.
	for (const auto hostRate : hostRates)
		for (const auto factor : oversamplingFactors)
			for (const auto feedback : { 2.2, 3.0, nonlinearTptKorg35MaximumFeedback })
				for (const auto knee : { 0.1, 1.0 })
					for (const auto highPass : { 0.0, 0.5, 1.0 })
					{
						const auto sampleRate = hostRate * factor;
						NonlinearTptKorg35 filter;
						filter.prepare(sampleRate);
						auto settings = settingsFor(1'000.0, feedback, knee);
						settings.driveDecibels = 24.0;
						settings.highPass = highPass;
						std::mt19937 random { 64 };
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
						INFO("rate " << hostRate << " x" << factor << ", rho " << feedback << ", K " << knee << ", high-pass " << highPass);
						CHECK(filter.diagnostics().nonFiniteSamples == 0);
						CHECK(filter.diagnostics().unconvergedSamples == 0);
						CHECK(filter.diagnostics().maximumIterations < nonlinearTptKorg35MaximumIterations);
						CHECK(std::isfinite(peak));
					}
}

namespace
{
// Two filters under the same input from different states: the state distance falls by at least 1e6 over a window
// scaled by the linear envelope rate w0 (7/3 - rho) / 2 (a time scale, not a bound).
double contractionRatio(double sampleRate, double cutoff, double feedback, double ratio, bool saw, double level, std::uint32_t seed,
	double highPass = 0.0)
{
	NonlinearTptKorg35 reference, perturbed;
	reference.prepare(sampleRate);
	perturbed.prepare(sampleRate);
	std::mt19937 random { seed };
	std::uniform_real_distribution state { -2.0, 2.0 };
	perturbed.setState(state(random), state(random));
	auto settings = settingsFor(cutoff, feedback);
	settings.highPass = highPass;
	const auto rate = std::numbers::pi * cutoff * (nonlinearTptKorg35Threshold - feedback);
	const auto total = static_cast<long>(4.0 * std::log(1.0e7) / rate * sampleRate);
	const auto period = static_cast<int>(std::lround(sampleRate / (cutoff * ratio)));
	double initial {}, last {};
	for (long sample = 0; sample < total; ++sample)
	{
		const auto phase = static_cast<double>(sample % period) / period;
		const auto input = level * (saw ? 2.0 * phase - 1.0 : std::sin(2.0 * std::numbers::pi * phase));
		juce::ignoreUnused(reference.process(input, settings), perturbed.process(input, settings));
		const auto [a1, a2] = reference.state();
		const auto [b1, b2] = perturbed.state();
		const auto distance = std::hypot(a1 - b1, a2 - b2);
		if (sample == 0) initial = distance;
		last = distance;
	}
	return last / initial;
}
}

TEST_CASE("Kobber Korg35 trajectories converge below threshold", "[kobber][filter][k35][k35-contraction]")
{
	// The incremental dynamics are a damped oscillator with damping 7/3 - rho h' >= 7/3 - rho > 0 (see the header),
	// whatever the input: two starting states must merge, from nearly linear levels to far past the knee.
	// The high-pass input enters the same incremental dynamics, so the high-pass and the half blend converge too.
	std::uint32_t seed = 65;
	for (const auto highPass : { 0.0, 0.5, 1.0 })
		for (const auto feedback : { 0.0, 1.5, 2.2, 2.3 })
			for (const auto [ratio, saw] : { std::pair { 1.0 / 3.0, true }, std::pair { 1.0, true }, std::pair { 1.0, false }, std::pair { 0.5, true } })
				for (const auto level : { 0.25, 1.0, 4.0, 16.0 })
				{
					INFO("high-pass " << highPass << ", rho " << feedback << ", ratio " << ratio << (saw ? " saw" : " sine") << ", level " << level);
					CHECK(contractionRatio(48'000.0, 1'000.0, feedback, ratio, saw, level, seed++, highPass) < 1.0e-6);
				}
}

namespace
{
// Zero-input growth exponent (per second) of the small-signal ring from a 1e-9 state.
double growthRate(double sampleRate, double cutoff, double feedback)
{
	NonlinearTptKorg35 filter;
	filter.prepare(sampleRate);
	filter.setState(1.0e-9, 0.0);
	const auto settings = settingsFor(cutoff, feedback);
	const auto window = std::max(4L, std::lround(sampleRate / cutoff));
	double n {}, sumT {}, sumY {}, sumTT {}, sumTY {}, peak {};
	for (long sample = 0; sample < window * 200; ++sample)
	{
		peak = std::max(peak, std::abs(filter.process(0.0, settings)));
		if ((sample + 1) % window != 0) continue;
		if (sample >= 4 * window)
		{
			const auto t = static_cast<double>(sample) / sampleRate, y = std::log(peak);
			n += 1.0;
			sumT += t;
			sumY += y;
			sumTT += t * t;
			sumTY += t * y;
		}
		peak = 0.0;
	}
	return (n * sumTY - sumT * sumY) / (n * sumTT - sumT * sumT);
}
}

TEST_CASE("Kobber Korg35 self-oscillation onset is at rho = 7/3 and the zero state stays at rest", "[kobber][filter][k35][k35-self-oscillation]")
{
	for (const auto hostRate : hostRates)
		for (const auto factor : oversamplingFactors)
		{
			INFO("rate " << hostRate << " x" << factor);
			CHECK(growthRate(hostRate * factor, 1'000.0, nonlinearTptKorg35Threshold - 0.01) < 0.0);
			CHECK(growthRate(hostRate * factor, 1'000.0, nonlinearTptKorg35Threshold + 0.01) > 0.0);
		}
	NonlinearTptKorg35 filter;
	filter.prepare(48'000.0);
	double peak {};
	for (int sample = 0; sample < 48'000; ++sample) peak = std::max(peak, std::abs(filter.process(0.0, settingsFor(1'000.0, 3.0))));
	CHECK(peak == 0.0);
}

namespace
{
// Harmonic distortion of a filtered 100 Hz sine at 96 kHz (whole periods), and its fundamental gain.
struct Distortion
{
	double thd {}, gain {};
};

template <typename Process>
Distortion sineDistortion(double amplitude, Process process)
{
	constexpr double sampleRate = 96'000.0;
	constexpr int period = 960;
	std::vector<double> y;
	for (int sample = 0; sample < 96'000 + 50 * period; ++sample)
	{
		const auto out = process(amplitude * std::sin(2.0 * std::numbers::pi * (sample % period) / period));
		if (sample >= 96'000) y.push_back(out);
	}
	double fundamental {}, harmonics {};
	for (int harmonic = 1; harmonic <= 20; ++harmonic)
	{
		const auto level = amplitudeAt(y, 100.0 * harmonic, sampleRate, period);
		if (harmonic == 1) fundamental = level;
		else harmonics += level * level;
	}
	return { std::sqrt(harmonics) / fundamental, fundamental / amplitude };
}
}

// Development characterization, hidden: passband grit, the gate for an early-Korg35 filter. Distortion of a 100 Hz sine
// against mixer level at Q 2 (rho 11/6; the SVF at 50 %), with the cutoff at 3 kHz (all measured harmonics in the
// passband) and at 300 Hz (most above the cutoff), at Drive-0 points L0 = 0.06 / 0.5 (the provisional voicing) / 1
// (knee 1 / L0). The Korg35 limiter sits on the filtered output, so harmonics above the cutoff are not filtered away
// (the comparison with the pre-filter saturator of the retired Reactive2P is in ADR 0007). THD in %, and the
// fundamental's gain in dB.
TEST_CASE("Kobber Korg35 passband distortion against level", "[.][k35-passband]")
{
	constexpr std::array levels { 0.125, 0.25, 0.5, 1.0, 2.0 };
	struct Candidate
	{
		std::string name;
		int kind; // 0 SVF, 2 Korg35
		double knee;
	};
	const std::vector<Candidate> candidates { { "SVF (knee 3, 50 % Resonance)", 0, 3.0 }, { "Korg35 L0 0.06, rho 11/6", 2, 1.0 / 0.06 },
		{ "Korg35 L0 0.5, rho 11/6", 2, 2.0 }, { "Korg35 L0 1, rho 11/6", 2, 1.0 } };
	constexpr std::array cutoffs { 3'000.0, 300.0 };
	const auto perCutoff = candidates.size() * levels.size();
	const auto cells = parallelMap(cutoffs.size() * perCutoff, [&](std::size_t job)
	{
		const auto cutoff = cutoffs[job / perCutoff];
		const auto& candidate = candidates[job % perCutoff / levels.size()];
		const auto level = levels[job % levels.size()];
		constexpr double sampleRate = 96'000.0;
		if (candidate.kind == 0)
		{
			vekt::kobber::NonlinearTptSvf svf;
			svf.prepare(sampleRate);
			const vekt::kobber::NonlinearTptSvfSettings settings { cutoff, vekt::kobber::svfDamping(0.5), 0.0, candidate.knee, vekt::kobber::svfDampingCurve };
			return sineDistortion(level, [&](double x) { return svf.process(x, settings).lowPass; });
		}
		NonlinearTptKorg35 filter;
		filter.prepare(sampleRate);
		const auto settings = settingsFor(cutoff, nonlinearTptKorg35Threshold - 0.5, candidate.knee);
		return sineDistortion(level, [&](double x) { return filter.process(x, settings); });
	});
	for (std::size_t c = 0; c < cutoffs.size(); ++c)
	{
		std::cout << "\n100 Hz sine, cutoff " << cutoffs[c] << " Hz: THD % (fundamental gain dB) at mixer level";
		for (const auto level : levels) std::cout << " | " << level;
		std::cout << "\n";
		for (std::size_t index = 0; index < candidates.size(); ++index)
		{
			std::cout << candidates[index].name;
			for (std::size_t level = 0; level < levels.size(); ++level)
			{
				const auto& cell = cells[c * perCutoff + index * levels.size() + level];
				std::cout << " | " << juce::String(100.0 * cell.thd, 2) << " (" << dB(cell.gain) << ")";
			}
			std::cout << "\n";
		}
	}
}

// Development measurement, hidden: self-oscillation RMS over the played signal's RMS (the unit-peak saw at mixer
// level 1, A2, through the same filter open at Resonance 0, so including its passband compression), the metric the
// Ladder (-13 to -14 dB) and the retired Reactive2P (+19 dB at L0 0.06, ADR 0007) were measured with. Knees K = 1 / L0.
TEST_CASE("Kobber Korg35 self-oscillation level re the played signal", "[.][k35-self-osc-level]")
{
	constexpr std::array driveZeros { 0.06, 0.24, 0.5, 1.0, 2.0 };
	constexpr std::array feedbacks { 2.4, 2.6, 3.0 };
	constexpr double sampleRate = 96'000.0;
	const auto passband = parallelMap(driveZeros.size(), [&](std::size_t index)
	{
		NonlinearTptKorg35 filter;
		filter.prepare(sampleRate);
		const auto settings = settingsFor(20'000.0, 0.0, 1.0 / driveZeros[index]);
		AdditiveSaw saw { 110.0, sampleRate };
		std::vector<float> out;
		for (long sample = 0; sample < static_cast<long>(sampleRate); ++sample)
		{
			const auto y = filter.process(saw.next(), settings);
			if (sample >= static_cast<long>(0.5 * sampleRate)) out.push_back(static_cast<float>(y));
		}
		return rmsOf(out);
	});
	const auto oscillation = parallelMap(driveZeros.size() * feedbacks.size(), [&](std::size_t job)
	{
		NonlinearTptKorg35 filter;
		filter.prepare(sampleRate);
		const auto knee = 1.0 / driveZeros[job / feedbacks.size()];
		filter.setState(1.0e-3 * knee, 0.0);
		const auto settings = settingsFor(1'000.0, feedbacks[job % feedbacks.size()], knee);
		std::vector<float> out;
		for (long sample = 0; sample < static_cast<long>(3.0 * sampleRate); ++sample)
		{
			const auto y = filter.process(0.0, settings);
			if (sample >= static_cast<long>(2.0 * sampleRate)) out.push_back(static_cast<float>(y));
		}
		return rmsOf(out);
	});
	std::cout << "\nKorg35 self-oscillation RMS re played saw RMS (dB) | L0 | passband RMS re unit saw (dB) | rho 2.4 | 2.6 | 3.0\n";
	for (std::size_t index = 0; index < driveZeros.size(); ++index)
	{
		std::cout << driveZeros[index] << " | " << dB(passband[index] * std::sqrt(3.0));
		for (std::size_t rho = 0; rho < feedbacks.size(); ++rho) std::cout << " | " << dB(oscillation[index * feedbacks.size() + rho] / passband[index]);
		std::cout << "\n";
	}
}

// Development audition, hidden: with VEKT_KOBBER_DUMP set, 96 kHz mono WAVs in k35/fixed-gain and k35/rms-matched of an
// A2 saw at mixer level 1 through the filter-envelope sweep (150 Hz -> 6 kHz -> 150 Hz) at the provisional voicing
// (Korg35Response.h: knee 2, the Resonance map to rho max 2.40): Resonance 80 % (Q 8) / 95 % (Q 100) / 97 % and 100 %
// (self-oscillating), Drive 0 / +6 / +12 dB; the SVF at 100 % Resonance, Drive 0 and +12 dB, for reference.
TEST_CASE("Kobber Korg35 audition renders", "[.][k35-renders]")
{
	const auto* directory = std::getenv("VEKT_KOBBER_DUMP");
	REQUIRE(directory != nullptr);
	const auto folder = juce::File(directory).getChildFile("k35");
	const auto fixedFolder = folder.getChildFile("fixed-gain"), matchedFolder = folder.getChildFile("rms-matched");
	for (const auto& old : { fixedFolder, matchedFolder })
		for (const auto& file : old.findChildFiles(juce::File::findFiles, false, "*.wav")) file.deleteFile();
	REQUIRE(fixedFolder.createDirectory().wasOk());
	REQUIRE(matchedFolder.createDirectory().wasOk());
	constexpr double sampleRate = 96'000.0, note = 110.0;
	const auto sweepCutoff = [](double t)
	{
		const auto position = t < 2.0 ? t / 2.0 : std::max(0.0, 1.0 - (t - 2.0) / 2.0);
		return 150.0 * std::pow(40.0, position);
	};
	struct Render
	{
		std::string name;
		bool svf {};
		double resonance {}, driveDb {};
	};
	std::vector<Render> renders;
	for (const auto resonance : { 0.8, 0.95, 0.97, 1.0 })
		for (const auto db : { 0.0, 6.0, 12.0 })
			renders.push_back({ "k35-sweep-res" + std::to_string(static_cast<int>(std::lround(resonance * 100.0))) + "-drive" + std::to_string(static_cast<int>(db)),
				false, resonance, db });
	for (const auto db : { 0.0, 12.0 }) renders.push_back({ "svf-sweep-res100-drive" + std::to_string(static_cast<int>(db)), true, 1.0, db });
	const auto outputs = parallelMap(renders.size(), [&](std::size_t index)
	{
		const auto& render = renders[index];
		AdditiveSaw saw { note, sampleRate };
		std::vector<float> out;
		vekt::kobber::NonlinearTptSvf svf;
		NonlinearTptKorg35 korg;
		svf.prepare(sampleRate);
		korg.prepare(sampleRate);
		vekt::kobber::NonlinearTptSvfSettings svfSettings { 150.0, vekt::kobber::svfDamping(1.0), render.driveDb, vekt::kobber::svfKnee, vekt::kobber::svfDampingCurve };
		auto korgSettings = settingsFor(150.0, vekt::kobber::korg35Feedback(render.resonance), vekt::kobber::korg35Knee);
		korgSettings.driveDecibels = render.driveDb;
		for (long sample = 0; sample < static_cast<long>(4.5 * sampleRate); ++sample)
		{
			const auto cutoff = sweepCutoff(static_cast<double>(sample) / sampleRate);
			const auto x = saw.next();
			double y {};
			if (render.svf)
			{
				svfSettings.cutoffHz = cutoff;
				y = vekt::kobber::svfOutputTrim(1.0) * svf.process(x, svfSettings).lowPass;
			}
			else
			{
				korgSettings.cutoffHz = cutoff;
				y = korg.process(x, korgSettings);
			}
			out.push_back(static_cast<float>(y));
		}
		return out;
	});
	double peak {};
	for (const auto& out : outputs)
		for (const auto sample : out) peak = std::max(peak, static_cast<double>(std::abs(sample)));
	REQUIRE(peak > 0.0);
	const auto fixedGain = std::pow(10.0, -1.0 / 20.0) / peak;
	std::cout << "\nfixed gain " << dB(fixedGain) << " dB; file | RMS in the fixed-gain set (dBFS)\n";
	for (std::size_t index = 0; index < renders.size(); ++index)
	{
		INFO(renders[index].name);
		REQUIRE(rmsOf(outputs[index]) > 0.0);
		writeWav(fixedFolder.getChildFile(renders[index].name + ".wav"), outputs[index], fixedGain, sampleRate);
		writeWav(matchedFolder.getChildFile(renders[index].name + ".wav"), outputs[index], 0.1 / rmsOf(outputs[index]), sampleRate);
		std::cout << renders[index].name << " | " << dB(fixedGain * rmsOf(outputs[index])) << "\n";
	}
}

TEST_CASE("Kobber K35 high-pass trim is unity up to the bell and eases to -3 dB at HP", "[kobber][filter][k35]")
{
	using vekt::kobber::korg35HighPassTrim;
	for (const auto mode : { -1.0, -0.5, 0.0 }) REQUIRE(std::bit_cast<std::uint64_t>(korg35HighPassTrim(mode)) == std::bit_cast<std::uint64_t>(1.0));
	REQUIRE(std::abs(20.0 * std::log10(korg35HighPassTrim(1.0)) + 3.0) < 1.0e-12);
	REQUIRE(std::bit_cast<std::uint64_t>(korg35HighPassTrim(1.5)) == std::bit_cast<std::uint64_t>(korg35HighPassTrim(1.0)));
	auto previous = 1.0;
	for (int step = 1; step <= 100; ++step)
	{
		const auto trim = korg35HighPassTrim(step / 100.0);
		REQUIRE(trim < previous);
		previous = trim;
	}
}

// The K35 Resonance map (Korg35Response.h): landmarks, rho max and the threshold crossing pinned, monotonic, C1 joins.
TEST_CASE("Kobber K35 Resonance map pins its landmarks and is monotonic and C1", "[kobber][filter][k35]")
{
	using vekt::kobber::korg35Feedback;
	const auto q = [](double resonance) { return 1.0 / (nonlinearTptKorg35Threshold - korg35Feedback(resonance)); };
	CHECK(std::abs(q(0.0) - 0.5) < 1.0e-12);
	CHECK(std::abs(q(0.8) - 8.0) < 1.0e-9);
	CHECK(std::abs(q(0.95) - 100.0) < 1.0e-9);
	CHECK(std::abs(korg35Feedback(1.0) - vekt::kobber::korg35MaximumFeedback) < 1.0e-12);
	CHECK(std::abs(vekt::kobber::korg35MaximumFeedback - 2.4) < 1.0e-15);
	// K35's own output trim, (1 + 4 r)^-0.8: unity at 0 %, about -11.2 dB at 100 %.
	CHECK(std::abs(vekt::kobber::korg35OutputTrim(0.0) - 1.0) < 1.0e-15);
	CHECK(std::abs(vekt::kobber::korg35OutputTrim(1.0) - std::pow(5.0, -0.8)) < 1.0e-15);
	// Only the top of the knob self-oscillates: the threshold 7/3 is crossed at 95.9 %.
	CHECK(korg35Feedback(0.958) < nonlinearTptKorg35Threshold);
	CHECK(korg35Feedback(0.960) > nonlinearTptKorg35Threshold);
	// Strictly increasing on a fine grid.
	for (int step = 1; step <= 100'000; ++step)
	{
		const auto r = step / 100'000.0;
		INFO("r " << r);
		REQUIRE(korg35Feedback(r) > korg35Feedback(r - 1.0e-5));
	}
	// One-sided derivatives agree at the knots and at the blend edges.
	constexpr double h = 1.0e-7;
	for (const auto r : { 0.76, 0.8, 0.84, 0.93, 0.95, 0.97 })
	{
		const auto left = (korg35Feedback(r) - korg35Feedback(r - h)) / h, right = (korg35Feedback(r + h) - korg35Feedback(r)) / h;
		INFO("r " << r << ": left " << left << ", right " << right);
		CHECK(std::abs(left - right) <= 1.0e-4 * std::max(std::abs(left), std::abs(right)));
	}
}

// Final prototype validation at the provisional product voicing (ADR 0007): knee 2 (L0 = 0.5 at mixer level 1, so
// Drive 0 / +6 / +12 / +24 dB is L = 0.5 / 1 / 2 / 8 for a unit-peak input) and rho from korg35Feedback, whose top
// (above 95.9 %) self-oscillates. The measures and policy follow ADR 0006/0007: narrow rational locking connected to
// the free oscillator is legitimate; wide detached divisions, runaway, non-finite states or large interior hysteresis
// are not.
namespace
{
using vekt::kobber::korg35Feedback;
using vekt::kobber::korg35Knee;
constexpr double topSampleRate = 96'000.0, topCutoff = 250.0;

enum class Stimulus { sine, saw, pulse };

const char* stimulusName(Stimulus stimulus)
{
	return stimulus == Stimulus::sine ? "sine" : stimulus == Stimulus::saw ? "saw" : "pulse";
}

double stimulusAt(Stimulus stimulus, double phase)
{
	switch (stimulus)
	{
	case Stimulus::sine: return std::sin(2.0 * std::numbers::pi * phase);
	case Stimulus::saw: return 2.0 * phase - 1.0;
	case Stimulus::pulse: return phase < 0.25 ? 1.5 : -0.5; // 25 % duty, DC-free: the asymmetric case
	}
	return 0.0;
}

// A K35 driven by a periodic input whose state and phase carry from step to step (quasi-static sweeps).
struct DrivenKorg
{
	NonlinearTptKorg35 filter;
	NonlinearTptKorg35Settings settings;
	double sampleRate {}, phase {}, level {};
	Stimulus stimulus {};

	DrivenKorg(double rate, double cutoff, double feedback, Stimulus kind, double inputLevel)
		: settings(settingsFor(cutoff, feedback, korg35Knee)), sampleRate(rate), level(inputLevel), stimulus(kind)
	{
		filter.prepare(rate);
		filter.setState(1.0e-3, 0.0);
	}

	double step(int period, NonlinearTptKorg35* shadow = nullptr)
	{
		const auto input = level * stimulusAt(stimulus, phase);
		const auto out = filter.process(input, settings);
		if (shadow != nullptr) juce::ignoreUnused(shadow->process(input, settings));
		phase += 1.0 / period;
		if (phase >= 1.0) phase -= 1.0;
		return out;
	}
};

struct Step
{
	double cents {}, perturbationRate {}, residualShare {}, rms {};
	int lockOrder {};
};

// As in ADR 0007's characterization: the perturbation rate (decay of a tiny state perturbation over w0: ~0 free,
// small entrained, large quenched), the residual share (energy not repeating with the input period), and the lock
// order (smallest q in 1..4 whose q-period residual is below 1e-3 while the phase is held, rate > 0.01).
Step measure(DrivenKorg& driven, int period, double settleSeconds, double windowSeconds)
{
	for (long sample = 0; sample < static_cast<long>(settleSeconds * driven.sampleRate); ++sample) juce::ignoreUnused(driven.step(period));
	const auto window = std::max(static_cast<long>(windowSeconds * driven.sampleRate), 24L * period);
	auto shadow = driven.filter;
	constexpr double perturbation = 1.0e-8;
	const auto distance = [&]
	{
		const auto [a1, a2] = driven.filter.state();
		const auto [b1, b2] = shadow.state();
		return std::hypot(b1 - a1, b2 - a2);
	};
	{
		const auto [a1, a2] = driven.filter.state();
		shadow.setState(a1 + perturbation, a2);
	}
	std::vector<double> y;
	double energy {}, logGrowth {};
	for (long sample = 0; sample < window; ++sample)
	{
		y.push_back(driven.step(period, &shadow));
		energy += y.back() * y.back();
		if (const auto current = distance(); current > 1.0e3 * perturbation || current < 1.0e-3 * perturbation)
		{
			logGrowth += std::log(std::max(current, 1.0e-300) / perturbation);
			const auto [a1, a2] = driven.filter.state();
			const auto [b1, b2] = shadow.state();
			const auto scale = current > 0.0 ? perturbation / current : 0.0;
			shadow.setState(a1 + (b1 - a1) * scale, a2 + (b2 - a2) * scale);
		}
	}
	logGrowth += std::log(std::max(distance(), 1.0e-300) / perturbation);
	Step result;
	result.perturbationRate = -logGrowth / (static_cast<double>(window) / driven.sampleRate) / (2.0 * std::numbers::pi * driven.settings.cutoffHz);
	result.rms = std::sqrt(energy / static_cast<double>(window));
	const auto residualShare = [&](int order)
	{
		const auto length = static_cast<std::size_t>(order * period);
		const auto repeats = y.size() / length;
		const auto used = repeats * length;
		std::vector<double> average(length);
		double total {}, residual {};
		for (std::size_t sample = 0; sample < used; ++sample)
		{
			average[sample % length] += y[sample] / static_cast<double>(repeats);
			total += y[sample] * y[sample];
		}
		for (std::size_t sample = 0; sample < used; ++sample)
		{
			const auto delta = y[sample] - average[sample % length];
			residual += delta * delta;
		}
		return total > 0.0 ? residual / total : 0.0;
	};
	result.residualShare = residualShare(1);
	if (result.perturbationRate > 0.01)
		for (int order = 1; order <= 4 && result.lockOrder == 0; ++order)
			if ((order == 1 ? result.residualShare : residualShare(order)) < 1.0e-3) result.lockOrder = order;
	return result;
}

// The settled free oscillation at a given rho (zero input, seeded): frequency re fc, peak and RMS re the knee, THD,
// even-harmonic share, and the relative drift of amplitude and frequency between two windows gapSeconds apart.
struct Oscillation
{
	double frequencyRatio {}, peak {}, rms {}, thd {}, evenShare {}, amplitudeDrift {}, frequencyDrift {};
	bool finite { true };
};

Oscillation freeOscillation(double sampleRate, double cutoff, double feedback, double gapSeconds)
{
	NonlinearTptKorg35 filter;
	filter.prepare(sampleRate);
	filter.setState(1.0e-3, 0.0);
	const auto settings = settingsFor(cutoff, feedback, korg35Knee);
	const auto settle = static_cast<long>(3.0 * sampleRate);
	const auto window = static_cast<long>(200.0 * sampleRate / cutoff);
	std::array<std::vector<double>, 2> windows;
	for (long sample = 0; sample < settle + 2 * window + static_cast<long>(gapSeconds * sampleRate); ++sample)
	{
		const auto y = filter.process(0.0, settings);
		if (sample >= settle && sample < settle + window) windows[0].push_back(y / korg35Knee);
		if (sample >= settle + window + static_cast<long>(gapSeconds * sampleRate)) windows[1].push_back(y / korg35Knee);
	}
	const auto a = analyseCycles(windows[0], sampleRate), b = analyseCycles(windows[1], sampleRate);
	Oscillation result;
	result.frequencyRatio = a.frequency / cutoff;
	result.peak = a.peak;
	result.rms = a.rms;
	result.thd = a.thd;
	result.evenShare = a.evenShare;
	result.amplitudeDrift = std::abs(b.fundamental / a.fundamental - 1.0);
	result.frequencyDrift = std::abs(b.frequency / a.frequency - 1.0);
	result.finite = std::isfinite(a.rms) && std::isfinite(b.rms) && filter.diagnostics().nonFiniteSamples == 0;
	return result;
}

// Lock tongue around R = f_in / f_osc (ADR 0007's tongue search): hold-in edges walking out from the
// centre with the state carried from inside, pull-in edges walking in from outside, each by coarse steps then a
// bisection that always steps in the walk's direction; a coarse upward scan reports locked islands elsewhere.
struct Tongue
{
	bool found {};
	Step centre;
	double holdLow {}, holdHigh {}, pullLow {}, pullHigh {};
	bool openLow {}, openHigh {};
	std::vector<std::pair<double, double>> islands;
};

struct TongueSearch
{
	double feedback, oscillatorHz, ratio;
	Stimulus stimulus;
	double level;
	int order;
	double rangeCents { 300.0 }, coarseCents { 25.0 };

	[[nodiscard]] int periodAt(double cents) const
	{
		return static_cast<int>(std::lround(topSampleRate / (ratio * oscillatorHz * std::exp2(cents / 1200.0))));
	}
	[[nodiscard]] double centsAt(int period) const { return 1200.0 * std::log2(topSampleRate / period / (ratio * oscillatorHz)); }
	[[nodiscard]] DrivenKorg fresh() const { return { topSampleRate, topCutoff, feedback, stimulus, level }; }
	Step probe(DrivenKorg& driven, int period, double settle) const
	{
		auto result = measure(driven, period, settle, 0.08);
		result.cents = centsAt(period);
		return result;
	}
	[[nodiscard]] bool locked(const Step& step) const { return step.lockOrder == order; }

	struct Edge
	{
		int lastSame {}, firstFlipped {};
		bool reachedEnd {};
	};

	Edge walk(DrivenKorg state, int from, bool startLocked, int limit) const
	{
		const auto direction = limit < from ? -1 : 1;
		auto current = from;
		for (;;)
		{
			if (current == limit) return { current, current, true };
			const auto stride = std::max(1, static_cast<int>(std::lround(current * (1.0 - std::exp2(-coarseCents / 1200.0)))));
			auto next = current + direction * stride;
			if ((direction < 0 && next < limit) || (direction > 0 && next > limit)) next = limit;
			auto trial = state;
			if (locked(probe(trial, next, 0.15)) == startLocked)
			{
				current = next;
				state = trial;
				continue;
			}
			auto same = current, flipped = next;
			while (std::abs(flipped - same) > 1)
			{
				const auto middle = same + (flipped - same) / 2;
				auto bisect = state;
				if (locked(probe(bisect, middle, 0.15)) == startLocked)
				{
					same = middle;
					state = bisect;
				}
				else flipped = middle;
			}
			return { same, flipped, false };
		}
	}

	[[nodiscard]] Tongue run() const
	{
		Tongue tongue;
		const auto lowest = periodAt(-rangeCents), highest = periodAt(rangeCents);
		auto centre = periodAt(0.0);
		auto state = fresh();
		tongue.centre = probe(state, centre, 0.3);
		if (!locked(tongue.centre))
			for (const auto offset : { 4.0, -4.0, 8.0, -8.0, 16.0, -16.0 })
			{
				auto candidate = fresh();
				const auto step = probe(candidate, periodAt(offset), 0.3);
				if (!locked(step)) continue;
				centre = periodAt(offset);
				state = candidate;
				tongue.centre = step;
				break;
			}
		tongue.found = locked(tongue.centre);
		if (tongue.found)
		{
			const auto up = walk(state, centre, true, highest), down = walk(state, centre, true, lowest);
			tongue.holdHigh = centsAt(up.lastSame);
			tongue.openHigh = up.reachedEnd;
			tongue.holdLow = centsAt(down.lastSame);
			tongue.openLow = down.reachedEnd;
			const auto pullIn = [&](bool fromBelow, double holdEdge, bool open) -> double
			{
				if (open) return holdEdge;
				const auto start = periodAt(fromBelow ? std::max(-rangeCents, holdEdge - 2.0 * coarseCents) : std::min(rangeCents, holdEdge + 2.0 * coarseCents));
				auto outside = fresh();
				if (locked(probe(outside, start, 0.3))) return centsAt(start);
				return centsAt(walk(outside, start, false, centre).firstFlipped);
			};
			tongue.pullLow = pullIn(true, tongue.holdLow, tongue.openLow);
			tongue.pullHigh = pullIn(false, tongue.holdHigh, tongue.openHigh);
		}
		auto scan = fresh();
		const auto count = static_cast<int>(std::lround(2.0 * rangeCents / coarseCents));
		double runStart = std::numeric_limits<double>::quiet_NaN(), runEnd {};
		for (int index = 0; index <= count; ++index)
		{
			const auto cents = -rangeCents + index * coarseCents;
			const auto inTongue = tongue.found && cents >= tongue.holdLow - coarseCents && cents <= tongue.holdHigh + coarseCents;
			const auto lockedHere = probe(scan, periodAt(cents), 0.06).lockOrder != 0 && !inTongue;
			if (lockedHere && std::isnan(runStart)) runStart = cents;
			if (lockedHere) runEnd = cents;
			if ((!lockedHere || index == count) && !std::isnan(runStart))
			{
				tongue.islands.emplace_back(runStart, runEnd);
				runStart = std::numeric_limits<double>::quiet_NaN();
			}
		}
		return tongue;
	}
};

std::string describe(const Tongue& tongue)
{
	std::string text;
	const auto c = [](double value) { return juce::String(value, 0).toStdString(); };
	if (!tongue.found) text = "none | - | -";
	else
		text = std::string(tongue.openLow || tongue.openHigh ? ">" : "") + c(tongue.holdHigh - tongue.holdLow) + " [" + c(tongue.holdLow) + ", "
			+ c(tongue.holdHigh) + "] | " + c(tongue.pullHigh - tongue.pullLow) + " | " + c(tongue.pullLow - tongue.holdLow) + "/"
			+ c(tongue.holdHigh - tongue.pullHigh);
	text += " | ";
	if (tongue.islands.empty()) text += "-";
	for (const auto& [low, high] : tongue.islands) text += "[" + c(low) + ", " + c(high) + "] ";
	return text;
}

constexpr std::array topResonances { 0.97, 1.0 };
constexpr std::array topDrives { 0.0, 6.0, 12.0, 24.0 };
double levelAt(double driveDb) { return 0.5 * korg35Knee * std::pow(10.0, driveDb / 20.0) / korg35Knee; } // unit-peak input, mixer units
}

// Solver health in product-like rendering: a unit-peak saw (mixer level 1, A2) at Resonance 95 / 97 / 100 %, Drive
// 0 / +6 / +12 / +24 dB, the cutoff swept four octaves around 1 kHz at 7 Hz, then zero input (free oscillation), over
// every host rate x oversampling factor.
TEST_CASE("Kobber K35 top of Resonance: solver health over the rate matrix", "[.][k35-top-solver]")
{
	struct Case
	{
		double rate;
		int factor;
	};
	std::vector<Case> cases;
	for (const auto hostRate : hostRates)
		for (const auto factor : oversamplingFactors) cases.push_back({ hostRate, factor });
	struct Result
	{
		std::uint64_t samples {}, iterations {}, fallbacks {}, nonFinite {}, unconverged {};
		int worst {};
		double peak {};
	};
	const auto results = parallelMap(cases.size(), [&](std::size_t index)
	{
		Result result;
		const auto sampleRate = cases[index].rate * cases[index].factor;
		for (const auto resonance : { 0.95, 0.97, 1.0 })
			for (const auto driveDb : topDrives)
			{
				NonlinearTptKorg35 filter;
				filter.prepare(sampleRate);
				auto settings = settingsFor(1'000.0, korg35Feedback(resonance), korg35Knee);
				settings.driveDecibels = driveDb;
				AdditiveSaw saw { 110.0, sampleRate };
				const auto total = static_cast<long>(0.3 * sampleRate);
				for (long sample = 0; sample < total; ++sample)
				{
					settings.cutoffHz = 1'000.0 * std::exp2(4.0 * std::sin(2.0 * std::numbers::pi * 7.0 * static_cast<double>(sample) / sampleRate));
					const auto y = filter.process(sample < total * 2 / 3 ? saw.next() : 0.0, settings);
					result.peak = std::max(result.peak, std::abs(y));
				}
				const auto& d = filter.diagnostics();
				result.samples += d.samples;
				result.iterations += d.iterations;
				result.fallbacks += d.fallbackSteps;
				result.nonFinite += d.nonFiniteSamples;
				result.unconverged += d.unconvergedSamples;
				result.worst = std::max(result.worst, d.maximumIterations);
			}
		return result;
	});
	std::cout << "\nrate x factor | mean iterations | worst | fallback steps | unconverged | non-finite | output peak (mixer units)\n";
	for (std::size_t index = 0; index < cases.size(); ++index)
	{
		const auto& r = results[index];
		std::cout << cases[index].rate << " x" << cases[index].factor << " | " << juce::String(static_cast<double>(r.iterations) / static_cast<double>(r.samples), 2)
				  << " | " << r.worst << " | " << r.fallbacks << " | " << r.unconverged << " | " << r.nonFinite << " | " << juce::String(r.peak, 2) << "\n";
		CHECK(r.nonFinite == 0);
		CHECK(r.unconverged == 0);
		CHECK(r.worst < nonlinearTptKorg35MaximumIterations);
		CHECK(r.peak < 20.0);
	}
}

// The free oscillation at the top of the knob (97 % and 100 %) and at fc/Fs 0.001 / 0.01 / 0.05: frequency re fc,
// peak re the knee, THD, even-harmonic share, and the drift of amplitude and frequency over 10 s.
TEST_CASE("Kobber K35 top of Resonance: free oscillation and long-term drift", "[.][k35-top-drift]")
{
	std::cout << "\nresonance (rho) | fc/Fs | f/fc | peak/K | THD % | even % | drift amplitude / frequency over 10 s\n";
	std::vector<std::pair<double, double>> jobs;
	for (const auto resonance : topResonances)
		for (const auto relative : { 0.001, 0.01, 0.05 }) jobs.emplace_back(resonance, relative);
	const auto results = parallelMap(jobs.size(), [&](std::size_t index)
	{ return freeOscillation(96'000.0, jobs[index].second * 96'000.0, korg35Feedback(jobs[index].first), 10.0); });
	for (std::size_t index = 0; index < jobs.size(); ++index)
	{
		const auto& r = results[index];
		std::cout << jobs[index].first * 100.0 << " % (" << juce::String(korg35Feedback(jobs[index].first), 3) << ") | " << jobs[index].second << " | "
				  << juce::String(r.frequencyRatio, 4) << " | " << juce::String(r.peak, 3) << " | " << juce::String(100.0 * r.thd, 2) << " | "
				  << juce::String(100.0 * r.evenShare, 5) << " | " << r.amplitudeDrift << " / " << r.frequencyDrift << "\n";
		CHECK(r.finite);
		CHECK(r.amplitudeDrift < 1.0e-4);
		CHECK(r.frequencyDrift < 1.0e-4);
	}
}

// Locking at the top of the knob: tongue searches around R = f_in / f_osc (1, 1/2, 1/3 at order 1; 2 at order 2, the
// division case) for a sine, saw and asymmetric pulse at Drive 0 / +6 / +12 / +24 dB (unit-peak input at mixer level
// 1), at 97 % and 100 % Resonance. Reported: hold-in width [edges], pull-in width, hysteresis low/high (cents; one
// input-period sample is about 1.5-9 c here), islands elsewhere, and at the centre the perturbation rate and residual.
TEST_CASE("Kobber K35 top of Resonance: locking and division map", "[.][k35-top-locking]")
{
	constexpr std::array ratios { std::pair { 1.0, 1 }, std::pair { 0.5, 1 }, std::pair { 1.0 / 3.0, 1 }, std::pair { 2.0, 2 } };
	constexpr std::array stimuli { Stimulus::sine, Stimulus::saw, Stimulus::pulse };
	const auto oscillators = parallelMap(topResonances.size(), [&](std::size_t index)
	{ return freeOscillation(topSampleRate, topCutoff, korg35Feedback(topResonances[index]), 0.1).frequencyRatio * topCutoff; });
	struct Job
	{
		std::size_t resonance;
		Stimulus stimulus;
		double ratio;
		int order;
		double driveDb;
	};
	std::vector<Job> jobs;
	for (std::size_t resonance = 0; resonance < topResonances.size(); ++resonance)
		for (const auto stimulus : stimuli)
			for (const auto [ratio, order] : ratios)
				for (const auto driveDb : topDrives) jobs.push_back({ resonance, stimulus, ratio, order, driveDb });
	const auto rows = parallelMap(jobs.size(), [&](std::size_t index)
	{
		const auto& job = jobs[index];
		const TongueSearch search { korg35Feedback(topResonances[job.resonance]), oscillators[job.resonance], job.ratio, job.stimulus, levelAt(job.driveDb), job.order };
		const auto tongue = search.run();
		return std::string(stimulusName(job.stimulus)) + " | " + juce::String(job.ratio, 3).toStdString() + " (" + std::to_string(job.order) + ") | +"
			+ std::to_string(static_cast<int>(job.driveDb)) + " | " + describe(tongue) + " | " + juce::String(tongue.centre.perturbationRate, 3).toStdString()
			+ " | " + juce::String(tongue.centre.residualShare, 3).toStdString();
	});
	for (std::size_t index = 0; index < jobs.size(); ++index)
	{
		if (index % (jobs.size() / topResonances.size()) == 0)
			std::cout << "\nResonance " << topResonances[jobs[index].resonance] * 100.0 << " % (rho " << juce::String(korg35Feedback(topResonances[jobs[index].resonance]), 3)
					  << ", f_osc " << juce::String(oscillators[jobs[index].resonance], 2) << " Hz at fc " << topCutoff
					  << ")\nstimulus | R (order) | drive dB | hold-in c [edges] | pull-in c | hysteresis low/high c | islands | rate at centre | residual share\n";
		std::cout << rows[index] << "\n";
	}
}

// Drive swept quasi-statically 0 -> +24 -> 0 dB in 1 dB steps at fixed cutoff (as a slow LFO on Drive would), for a
// saw with f_osc between its 2nd and 3rd harmonics (R 0.4) and a detuned sine (R 0.7), at 97 % and 100 %: lock order /
// perturbation rate / residual share per step, and the lock and quench thresholds up and down (hysteresis in dB).
TEST_CASE("Kobber K35 top of Resonance: Drive sweep through lock and quench", "[.][k35-top-drive-sweep]")
{
	constexpr std::array stimuli { std::pair { Stimulus::saw, 0.4 }, std::pair { Stimulus::sine, 0.7 } };
	const auto blocks = parallelMap(stimuli.size() * topResonances.size(), [&](std::size_t job)
	{
		const auto [stimulus, ratio] = stimuli[job / topResonances.size()];
		const auto resonance = topResonances[job % topResonances.size()];
		const auto feedback = korg35Feedback(resonance);
		const auto oscillator = freeOscillation(topSampleRate, topCutoff, feedback, 0.1).frequencyRatio * topCutoff;
		const auto period = static_cast<int>(std::lround(topSampleRate / (ratio * oscillator)));
		DrivenKorg driven(topSampleRate, topCutoff, feedback, stimulus, levelAt(0.0));
		std::string text = "\n" + std::string(stimulusName(stimulus)) + " R " + juce::String(ratio, 1).toStdString() + ", Resonance "
			+ juce::String(resonance * 100.0, 0).toStdString() + " % | drive dB: order/rate/residual, up then down\n";
		int firstLockUp = -1, lastLockDown = -1, firstQuenchUp = -1, lastQuenchDown = -1;
		std::vector<int> drives;
		for (int drive = 0; drive <= 24; ++drive) drives.push_back(drive);
		for (int drive = 23; drive >= 0; --drive) drives.push_back(drive);
		for (std::size_t index = 0; index < drives.size(); ++index)
		{
			const auto drive = drives[index];
			const auto upward = index <= 24;
			driven.settings.driveDecibels = drive;
			const auto step = measure(driven, period, 0.06, 0.08);
			text += (upward ? "+" : "-") + std::to_string(drive) + ": " + std::to_string(step.lockOrder) + "/" + juce::String(step.perturbationRate, 2).toStdString()
				+ "/" + juce::String(step.residualShare, 2).toStdString() + (index % 7 == 6 ? "\n" : "  ");
			const auto locked = step.lockOrder != 0, quenched = locked && step.perturbationRate > 0.2;
			if (upward && locked && firstLockUp < 0) firstLockUp = drive;
			if (upward && quenched && firstQuenchUp < 0) firstQuenchUp = drive;
			if (!upward && locked) lastLockDown = drive;
			if (!upward && quenched) lastQuenchDown = drive;
		}
		return text + "\nlock from +" + std::to_string(firstLockUp) + " dB up, held to +" + std::to_string(lastLockDown) + " dB down; quench (rate > 0.2) from +"
			+ std::to_string(firstQuenchUp) + " dB up, to +" + std::to_string(lastQuenchDown) + " dB down (-1 = never)\n";
	});
	for (const auto& block : blocks) std::cout << block;
}

namespace
{
// In-place radix-2 FFT (power-of-two length).
void fft(std::vector<std::complex<double>>& data)
{
	const auto n = data.size();
	for (std::size_t i = 1, j = 0; i < n; ++i)
	{
		auto bit = n >> 1;
		for (; (j & bit) != 0; bit >>= 1) j ^= bit;
		j ^= bit;
		if (i < j) std::swap(data[i], data[j]);
	}
	for (std::size_t length = 2; length <= n; length <<= 1)
	{
		const auto angle = -2.0 * std::numbers::pi / static_cast<double>(length);
		const std::complex<double> unit(std::cos(angle), std::sin(angle));
		for (std::size_t start = 0; start < n; start += length)
		{
			std::complex<double> w(1.0, 0.0);
			for (std::size_t k = 0; k < length / 2; ++k)
			{
				const auto even = data[start + k], odd = data[start + k + length / 2] * w;
				data[start + k] = even + odd;
				data[start + k + length / 2] = even - odd;
				w *= unit;
			}
		}
	}
}

// Energy in 20 Hz - 20 kHz that is not at a harmonic of f0 (+-4 bins of a Blackman-Harris window), over all energy
// in that band, in dB: aliasing folded into the audio band (and any other inharmonic content) in a periodic steady
// state. DC is excluded (reported separately): the limiter partly rectifies inputs without half-wave symmetry.
struct Spectrum
{
	double inharmonicDb {}, dcDb {}; // dcDb: the output mean over its RMS
};

Spectrum nonharmonicDb(const std::vector<double>& y, double sampleRate, double fundamental)
{
	std::size_t n = 1;
	while (n * 2 <= y.size()) n *= 2;
	std::vector<std::complex<double>> data(n);
	for (std::size_t i = 0; i < n; ++i)
	{
		const auto x = 2.0 * std::numbers::pi * (static_cast<double>(i) + 0.5) / static_cast<double>(n);
		const auto w = 0.35875 - 0.48829 * std::cos(x) + 0.14128 * std::cos(2.0 * x) - 0.01168 * std::cos(3.0 * x);
		data[i] = w * y[y.size() - n + i];
	}
	fft(data);
	const auto binWidth = sampleRate / static_cast<double>(n);
	double harmonic {}, other {};
	for (std::size_t bin = 1; bin < n / 2 && static_cast<double>(bin) * binWidth < 20'000.0; ++bin)
	{
		const auto frequency = static_cast<double>(bin) * binWidth;
		if (frequency < 20.0) continue;
		const auto power = std::norm(data[bin]);
		const auto nearest = std::round(frequency / fundamental) * fundamental;
		if (nearest > 0.0 && std::abs(frequency - nearest) <= 4.0 * binWidth) harmonic += power;
		else other += power;
	}
	double mean {}, energy {};
	for (const auto value : y)
	{
		mean += value;
		energy += value * value;
	}
	mean /= static_cast<double>(y.size());
	return { 10.0 * std::log10(other / (harmonic + other)), 20.0 * std::log10(std::abs(mean) / std::sqrt(energy / static_cast<double>(y.size())) + 1.0e-300) };
}
}

// Aliasing: a band-limited saw (harmonics below 20 kHz) at 48 kHz x 1 / 2 / 4 / 8 through K35 at 90 % Resonance
// (Q 43: below threshold, so the steady state is periodic and all inharmonic energy is aliasing or numerical noise),
// cutoff 2 kHz and 8 kHz, Drive 0 / +12 / +24 dB, notes 220 / 880 / 1760.3 Hz; the SVF at 100 % Resonance (its
// maximum, Q 8) and the same Drive for comparison. Inharmonic energy below 20 kHz re the total, in dB, measured at the
// oversampled rate (the product's decimation removes content above 20 kHz, not what has already folded below it).
// Also the DC offset re the output RMS.
TEST_CASE("Kobber K35 aliasing against oversampling", "[.][k35-aliasing]")
{
	struct Job
	{
		int factor;
		double cutoff, driveDb, note;
		bool svf;
	};
	std::vector<Job> jobs;
	for (const auto svf : { false, true })
		for (const auto cutoff : { 2'000.0, 8'000.0 })
			for (const auto driveDb : { 0.0, 12.0, 24.0 })
				for (const auto note : { 220.0, 880.0, 1'760.3 })
					for (const auto factor : oversamplingFactors) jobs.push_back({ factor, cutoff, driveDb, note, svf });
	const auto results = parallelMap(jobs.size(), [&](std::size_t index)
	{
		const auto& job = jobs[index];
		const auto sampleRate = 48'000.0 * job.factor;
		AdditiveSaw saw { job.note, sampleRate };
		NonlinearTptKorg35 korg;
		vekt::kobber::NonlinearTptSvf svf;
		korg.prepare(sampleRate);
		svf.prepare(sampleRate);
		auto korgSettings = settingsFor(job.cutoff, korg35Feedback(0.9), korg35Knee);
		korgSettings.driveDecibels = job.driveDb;
		const vekt::kobber::NonlinearTptSvfSettings svfSettings { job.cutoff, vekt::kobber::svfDamping(1.0), job.driveDb, vekt::kobber::svfKnee,
			vekt::kobber::svfDampingCurve };
		std::vector<double> y;
		const auto total = static_cast<long>(1.0 * sampleRate);
		for (long sample = 0; sample < total; ++sample)
		{
			const auto x = saw.next();
			const auto out = job.svf ? svf.process(x, svfSettings).lowPass : korg.process(x, korgSettings);
			if (sample >= total / 4) y.push_back(out);
		}
		return nonharmonicDb(y, sampleRate, job.note);
	});
	std::cout << "\ninharmonic energy in 20 Hz - 20 kHz, dB re total | 1x | 2x | 4x | 8x | DC re RMS (dB, at 8x)\n";
	for (std::size_t index = 0; index < jobs.size(); index += oversamplingFactors.size())
	{
		const auto& job = jobs[index];
		std::cout << (job.svf ? "SVF 100 %" : "K35 90 %") << ", cutoff " << job.cutoff << ", +" << job.driveDb << " dB, note " << job.note;
		for (std::size_t factor = 0; factor < oversamplingFactors.size(); ++factor) std::cout << " | " << juce::String(results[index + factor].inharmonicDb, 1);
		std::cout << " | " << juce::String(results[index + oversamplingFactors.size() - 1].dcDb, 1) << "\n";
	}
}

// Development measurement, hidden: why the Ladder's high-pass can sound harsh at high cutoffs. A band-limited saw (A2,
// 110 Hz, level 0.7, Drive 0) at 48 kHz x 1 / 2 / 4 / 8 through the Ladder at Mode +1 and -1, the SVF's high-pass and
// K35's high-pass, cutoff 1 / 2 / 4 kHz, Resonance 0 / 50 / 90 % (below every filter's threshold, so the steady state is
// periodic). Inharmonic energy below 20 kHz re the total, at the oversampled rate: what falls with oversampling is
// aliasing; what stays is not.
TEST_CASE("Kobber Ladder high-pass aliasing against oversampling", "[.][ladder-hp-aliasing]")
{
	enum class Kind { ladderHighPass, ladderLowPass, svfHighPass, k35HighPass, highPassLadder };
	struct Job
	{
		Kind kind;
		double cutoff, resonance;
		int factor;
	};
	std::vector<Job> jobs;
	for (const auto kind : { Kind::ladderHighPass, Kind::ladderLowPass, Kind::svfHighPass, Kind::k35HighPass, Kind::highPassLadder })
		for (const auto cutoff : { 1'000.0, 2'000.0, 4'000.0 })
			for (const auto resonance : { 0.0, 0.5, 0.9 })
				for (const auto factor : oversamplingFactors) jobs.push_back({ kind, cutoff, resonance, factor });
	constexpr double note = 110.0, level = 0.7;
	const auto results = parallelMap(jobs.size(), [&](std::size_t index)
	{
		const auto& job = jobs[index];
		const auto sampleRate = 48'000.0 * job.factor;
		AdditiveSaw saw { note, sampleRate };
		vekt::kobber::NonlinearTptLadder ladder;
		vekt::kobber::NonlinearTptSvf svf;
		NonlinearTptKorg35 korg;
		ladder.prepare(sampleRate);
		svf.prepare(sampleRate);
		korg.prepare(sampleRate);
		vekt::kobber::NonlinearTptLadderHighPass highPassLadder;
		highPassLadder.prepare(sampleRate);
		vekt::kobber::NonlinearTptLadderHighPassSettings highPassSettings;
		highPassSettings.cutoffHz = job.cutoff;
		highPassSettings.feedback = vekt::kobber::ladderHighPassFeedback(job.resonance);
		const vekt::kobber::NonlinearTptLadderSettings ladderSettings { static_cast<float>(job.cutoff), static_cast<float>(job.resonance), 0.0f,
			false, 0.0f, job.kind == Kind::ladderHighPass ? 1.0f : -1.0f };
		const vekt::kobber::NonlinearTptSvfSettings svfSettings { job.cutoff, vekt::kobber::svfDamping(job.resonance), 0.0, vekt::kobber::svfKnee,
			vekt::kobber::svfDampingCurve };
		auto korgSettings = settingsFor(job.cutoff, korg35Feedback(job.resonance), korg35Knee);
		korgSettings.highPass = 1.0;
		std::vector<double> y;
		const auto total = static_cast<long>(1.0 * sampleRate);
		for (long sample = 0; sample < total; ++sample)
		{
			const auto x = level * saw.next();
			double out {};
			switch (job.kind)
			{
			case Kind::ladderHighPass:
			case Kind::ladderLowPass: out = ladder.processCoupled(static_cast<float>(x), ladderSettings); break;
			case Kind::svfHighPass: out = svf.process(x, svfSettings).highPass; break;
			case Kind::k35HighPass: out = korg.process(x, korgSettings); break;
			case Kind::highPassLadder: out = highPassLadder.process(x, highPassSettings); break;
			}
			if (sample >= total / 4) y.push_back(out);
		}
		return nonharmonicDb(y, sampleRate, note);
	});
	constexpr std::array kindNames { "Ladder tap HP (raw)", "Ladder LP", "SVF HP", "K35 HP", "Ladder HP (high-pass ladder)" };
	std::cout << "\ninharmonic energy in 20 Hz - 20 kHz, dB re total | 1x | 2x | 4x | 8x\n";
	for (std::size_t index = 0; index < jobs.size(); index += oversamplingFactors.size())
	{
		const auto& job = jobs[index];
		std::cout << kindNames[static_cast<std::size_t>(job.kind)] << ", cutoff " << job.cutoff << ", Res " << 100.0 * job.resonance << " %";
		for (std::size_t factor = 0; factor < oversamplingFactors.size(); ++factor) std::cout << " | " << juce::String(results[index + factor].inharmonicDb, 1);
		std::cout << "\n";
	}
}

// Development measurement, hidden: how much of each filter's high-pass is its own non-linear residue (distortion,
// intermodulation and aliasing) on a Classic Three Bass-like mix (a 65.4 Hz sine at 1, a 130.6 Hz sine at 0.72 and a
// 131.1 Hz band-limited saw at 0.54, as its 16' / 8' -3 ct / 8' +4 ct oscillators), 48 kHz, Drive 0, Mode +1. The linear
// reference is the same filter at 1e-2 of the input, scaled back up (not smaller: the Ladder's solve tolerance is
// absolute). Each filter's input can be lowered by the given dB
// with the output made up, as the Ladder's high-pass now does. Residue re the linear output, in dB.
TEST_CASE("Kobber high-pass non-linear residue on a detuned mix", "[.][kobber-hp-distortion]")
{
	enum class Kind { ladder, svf, k35 };
	struct Job { Kind kind; double cutoff, resonance, reductionDb; };
	std::vector<Job> jobs;
	for (const auto kind : { Kind::ladder, Kind::svf, Kind::k35 })
		for (const auto cutoff : { 1'000.0, 3'000.0 })
			for (const auto resonance : { 0.0, 0.5, 0.9 })
				for (const auto reductionDb : { 0.0, 6.0, 12.0, 18.0 }) jobs.push_back({ kind, cutoff, resonance, reductionDb });
	constexpr double sampleRate = 48'000.0;
	const auto render = [](const Job& job, double scale)
	{
		vekt::kobber::NonlinearTptLadder ladder;
		vekt::kobber::NonlinearTptSvf svf;
		NonlinearTptKorg35 korg;
		ladder.prepare(sampleRate);
		svf.prepare(sampleRate);
		korg.prepare(sampleRate);
		AdditiveSaw saw { 131.1, sampleRate };
		const vekt::kobber::NonlinearTptLadderSettings ladderSettings { static_cast<float>(job.cutoff), static_cast<float>(job.resonance), 0.0f, false, 0.0f, 1.0f };
		const vekt::kobber::NonlinearTptSvfSettings svfSettings { job.cutoff, vekt::kobber::svfDamping(job.resonance), 0.0, vekt::kobber::svfKnee,
			vekt::kobber::svfDampingCurve };
		auto korgSettings = settingsFor(job.cutoff, korg35Feedback(job.resonance), korg35Knee);
		korgSettings.highPass = 1.0;
		const auto gain = scale * std::pow(10.0, -job.reductionDb / 20.0);
		std::vector<double> y;
		for (long sample = 0; sample < static_cast<long>(1.5 * sampleRate); ++sample)
		{
			const auto t = static_cast<double>(sample) / sampleRate;
			const auto x = gain * (std::sin(2.0 * std::numbers::pi * 65.4 * t) + 0.72 * std::sin(2.0 * std::numbers::pi * 130.6 * t) + 0.54 * saw.next());
			double out {};
			switch (job.kind)
			{
			case Kind::ladder: out = ladder.processCoupled(static_cast<float>(x), ladderSettings); break;
			case Kind::svf: out = svf.process(x, svfSettings).highPass; break;
			case Kind::k35: out = korg.process(x, korgSettings); break;
			}
			if (sample >= static_cast<long>(0.5 * sampleRate)) y.push_back(out / gain);
		}
		return y;
	};
	const auto residues = parallelMap(jobs.size(), [&](std::size_t index)
	{
		const auto nonlinear = render(jobs[index], 1.0), linear = render(jobs[index], 1.0e-2);
		double residue {}, energy {};
		for (std::size_t sample = 0; sample < nonlinear.size(); ++sample)
		{
			residue += (nonlinear[sample] - linear[sample]) * (nonlinear[sample] - linear[sample]);
			energy += linear[sample] * linear[sample];
		}
		return 10.0 * std::log10(residue / energy);
	});
	constexpr std::array kindNames { "Ladder", "SVF", "K35" };
	std::cout << "\nHP non-linear residue re the linear output, dB | input 0 / -6 / -12 / -18 dB\n";
	for (std::size_t index = 0; index < jobs.size(); index += 4)
	{
		const auto& job = jobs[index];
		std::cout << kindNames[static_cast<std::size_t>(job.kind)] << " HP, cutoff " << job.cutoff << ", Res " << 100.0 * job.resonance << " %";
		for (std::size_t step = 0; step < 4; ++step) std::cout << " | " << juce::String(residues[index + step], 1);
		std::cout << "\n";
	}
}

// Development measurement, hidden: the Ladder's non-linear residue at high Resonance (below its oscillation onset,
// about 98.4 %), Mode +1 and -1, on the same detuned mix as [kobber-hp-distortion], for input reductions with make-up.
TEST_CASE("Kobber Ladder residue at high Resonance", "[.][ladder-high-resonance-residue]")
{
	struct Job { float mode; double cutoff, resonance, reductionDb; };
	std::vector<Job> jobs;
	for (const auto mode : { 1.0f, -1.0f })
		for (const auto cutoff : { 1'000.0, 3'000.0 })
			for (const auto resonance : { 0.5, 0.8, 0.9, 0.95, 0.97, 0.98 })
				for (const auto reductionDb : { 0.0, 18.0, 24.0, 30.0 }) jobs.push_back({ mode, cutoff, resonance, reductionDb });
	constexpr double sampleRate = 48'000.0;
	const auto render = [](const Job& job, double scale)
	{
		vekt::kobber::NonlinearTptLadder ladder;
		ladder.prepare(sampleRate);
		AdditiveSaw saw { 131.1, sampleRate };
		const vekt::kobber::NonlinearTptLadderSettings settings { static_cast<float>(job.cutoff), static_cast<float>(job.resonance), 0.0f, false, 0.0f, job.mode };
		const auto gain = scale * std::pow(10.0, -job.reductionDb / 20.0);
		std::vector<double> y;
		for (long sample = 0; sample < static_cast<long>(2.0 * sampleRate); ++sample)
		{
			const auto t = static_cast<double>(sample) / sampleRate;
			const auto x = gain * (std::sin(2.0 * std::numbers::pi * 65.4 * t) + 0.72 * std::sin(2.0 * std::numbers::pi * 130.6 * t) + 0.54 * saw.next());
			const auto out = ladder.processCoupled(static_cast<float>(x), settings);
			if (sample >= static_cast<long>(1.0 * sampleRate)) y.push_back(out / gain);
		}
		return y;
	};
	const auto residues = parallelMap(jobs.size(), [&](std::size_t index)
	{
		const auto nonlinear = render(jobs[index], 1.0), linear = render(jobs[index], 1.0e-2);
		double residue {}, energy {};
		for (std::size_t sample = 0; sample < nonlinear.size(); ++sample)
		{
			residue += (nonlinear[sample] - linear[sample]) * (nonlinear[sample] - linear[sample]);
			energy += linear[sample] * linear[sample];
		}
		return 10.0 * std::log10(residue / energy);
	});
	std::cout << "\nLadder non-linear residue re the linear output, dB | input 0 / -18 / -24 / -30 dB\n";
	for (std::size_t index = 0; index < jobs.size(); index += 4)
	{
		const auto& job = jobs[index];
		std::cout << (job.mode > 0.0f ? "HP" : "LP") << ", cutoff " << job.cutoff << ", Res " << 100.0 * job.resonance << " %";
		for (std::size_t step = 0; step < 4; ++step) std::cout << " | " << juce::String(residues[index + step], 1);
		std::cout << "\n";
	}
}

TEST_CASE("Kobber K35 batched lanes match the scalar solve", "[kobber][filter][k35]")
{
	// Groups of four and two share a vector tanh (about 2 ulp from libm), the rest run the scalar solve; below
	// threshold the filter contracts, so the lanes stay within rounding of the scalar filters under hostile input.
	// Lanes with shared settings (a voice's unison layers) and with their own (different voices) both.
	for (const auto lanes : { std::size_t { 4 }, std::size_t { 3 }, std::size_t { 2 } })
		for (const auto sharedSettings : { true, false })
		{
			INFO(lanes << " lanes, shared settings " << sharedSettings);
			std::array<NonlinearTptKorg35, 4> batched, scalar;
			std::array<NonlinearTptKorg35Settings, 4> settings;
			for (std::size_t lane = 0; lane < 4; ++lane)
			{
				batched[lane].prepare(96'000.0);
				scalar[lane].prepare(96'000.0);
				settings[lane] = settingsFor(1'000.0, sharedSettings ? 2.2 : 1.0 + 0.3 * static_cast<double>(lane), korg35Knee);
				settings[lane].driveDecibels = 12.0;
				// Shared: the half blend; own: low-pass to high-pass across the lanes.
				settings[lane].highPass = sharedSettings ? 0.5 : static_cast<double>(lane) / 3.0;
			}
			std::array<NonlinearTptKorg35*, 4> filters {};
			std::array<const NonlinearTptKorg35Settings*, 4> laneSettings {};
			for (std::size_t lane = 0; lane < 4; ++lane)
			{
				filters[lane] = &batched[lane];
				laneSettings[lane] = sharedSettings ? &settings[0] : &settings[lane];
			}
			std::mt19937 random { 91 };
			std::uniform_real_distribution noise { -2.0, 2.0 };
			double peak {}, error {};
			for (int sample = 0; sample < 20'000; ++sample)
			{
				const auto cutoff = 1'000.0 * std::exp2(3.0 * std::sin(0.001 * sample));
				for (auto& laneSetting : settings) laneSetting.cutoffHz = cutoff;
				std::array<double, 4> inputs {}, outputs {};
				for (std::size_t lane = 0; lane < lanes; ++lane) inputs[lane] = noise(random);
				NonlinearTptKorg35::processLanes(std::span<NonlinearTptKorg35* const>(filters.data(), lanes), std::span<const double>(inputs.data(), lanes),
					std::span(outputs.data(), lanes), std::span<const NonlinearTptKorg35Settings* const>(laneSettings.data(), lanes));
				for (std::size_t lane = 0; lane < lanes; ++lane)
				{
					const auto reference = scalar[lane].process(inputs[lane], *laneSettings[lane]);
					peak = std::max(peak, std::abs(reference));
					error = std::max(error, std::abs(outputs[lane] - reference));
				}
			}
			INFO("error " << error << " of peak " << peak);
			REQUIRE(peak > 0.0);
			CHECK(error <= 1.0e-9 * peak);
			for (std::size_t lane = 0; lane < lanes; ++lane)
			{
				CHECK(batched[lane].diagnostics().nonFiniteSamples == 0);
				CHECK(batched[lane].diagnostics().unconvergedSamples == 0);
				CHECK(batched[lane].diagnostics().samples == 20'000u);
			}
		}
}

namespace
{
// Steady-state gain of a small sine (near-linear) through K35 with the given high-pass blend, after two seconds of settling.
double k35HighPassGain(double frequency, double cutoff, double feedback, double highPass)
{
	constexpr double sampleRate = 96'000.0, amplitude = 1.0e-4;
	NonlinearTptKorg35 filter;
	filter.prepare(sampleRate);
	auto settings = settingsFor(cutoff, feedback, korg35Knee);
	settings.highPass = highPass;
	const auto settle = static_cast<long>(2.0 * sampleRate);
	const auto periods = std::max(4.0, std::floor(0.5 * frequency));
	const auto measure = static_cast<long>(std::lround(periods * sampleRate / frequency));
	double inPhase {}, quadrature {};
	for (long sample = 0; sample < settle + measure; ++sample)
	{
		const auto phase = 2.0 * std::numbers::pi * frequency * static_cast<double>(sample) / sampleRate;
		const auto out = filter.process(amplitude * std::sin(phase), settings);
		if (sample < settle) continue;
		inPhase += out * std::sin(phase);
		quadrature += out * std::cos(phase);
	}
	return 2.0 * std::hypot(inPhase, quadrature) / static_cast<double>(measure) / amplitude;
}
}

TEST_CASE("Kobber Korg35 high-pass input is 6 dB/oct below the cutoff, unity above, and flat at the half blend for rho = 1",
	"[kobber][filter][k35][k35-highpass]")
{
	// Small-signal, y = [x_lp + (p^2 + 4/3 p) x_hp] / (p^2 + (7/3 - rho) p + 1) (NonlinearTptKorg35.h, ADR 0007). The
	// half blend's numerator 0.5 (p^2 + 4/3 p + 1) is half the denominator at rho = 1: -6 dB at every frequency.
	constexpr double cutoff = 1'000.0, lowRho = 7.0 / 3.0 - 2.0; // Q 0.5
	const auto octave = 20.0 * std::log10(k35HighPassGain(cutoff / 8.0, cutoff, lowRho, 1.0) / k35HighPassGain(cutoff / 16.0, cutoff, lowRho, 1.0));
	const auto passband = 20.0 * std::log10(k35HighPassGain(cutoff * 8.0, cutoff, lowRho, 1.0));
	INFO("slope " << octave << " dB per octave, passband " << passband << " dB");
	CHECK(std::abs(octave - 6.0) < 0.5);
	CHECK(std::abs(passband) < 0.5);
	for (const auto ratio : { 0.125, 0.5, 1.0, 2.0, 8.0 })
	{
		const auto flat = 20.0 * std::log10(k35HighPassGain(cutoff * ratio, cutoff, 1.0, 0.5));
		INFO("half blend, f / fc " << ratio << ": " << flat << " dB");
		CHECK(std::abs(flat + 6.02) < 0.2);
	}
}
