#include <audio_lab/LadderPrototype.h>
#include <audio_lab/KobberCostTimingRule.h>
#include <audio_lab/NonlinearTptLadderReference.h>
#include <LadderResonance.h>

#include <vekt/audio_analysis/Measurements.h>
#include <vekt/dsp/OversamplingBank.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <vector>

namespace
{
void requireConverged(const juce::var& section)
{
	const auto* entries = section.getArray();
	REQUIRE(entries != nullptr);
	for (const auto& entry : *entries)
	{
		const auto solver = entry.getProperty("solver", {});
		REQUIRE(static_cast<juce::int64>(solver.getProperty("unconverged_samples", -1)) == 0);
		REQUIRE(static_cast<juce::int64>(solver.getProperty("non_finite_samples", -1)) == 0);
	}
}

void requireFinite(const juce::var& section)
{
	const auto* entries = section.getArray();
	REQUIRE(entries != nullptr);
	for (const auto& entry : *entries)
		REQUIRE(static_cast<juce::int64>(entry.getProperty("stability", {})
			.getProperty("non_finite_samples", -1)) == 0);
}
}

TEST_CASE("Kobber measured timing rule requires strict headroom and zero exceedances", "[audio-lab][kobber][ladder-cost-rule]")
{
	using vekt::audio_lab::meetsMonoMeasuredTimingRule;
	using vekt::audio_lab::wallOverrunWithCpuBelowDeadline;
	REQUIRE(meetsMonoMeasuredTimingRule(749.0, 1000.0, 0));
	REQUIRE_FALSE(meetsMonoMeasuredTimingRule(750.0, 1000.0, 0));
	REQUIRE_FALSE(meetsMonoMeasuredTimingRule(751.0, 1000.0, 0));
	REQUIRE_FALSE(meetsMonoMeasuredTimingRule(749.0, 1000.0, 1));
	REQUIRE_FALSE(meetsMonoMeasuredTimingRule(0.0, 0.0, 0));
	REQUIRE_FALSE(meetsMonoMeasuredTimingRule(-1.0, 1000.0, 0));
	REQUIRE_FALSE(meetsMonoMeasuredTimingRule(std::numeric_limits<double>::quiet_NaN(), 1000.0, 0));
	REQUIRE_FALSE(meetsMonoMeasuredTimingRule(0.0, std::numeric_limits<double>::infinity(), 0));
	REQUIRE(wallOverrunWithCpuBelowDeadline(1200.0, 600.0, 1000.0));
	REQUIRE_FALSE(wallOverrunWithCpuBelowDeadline(1200.0, 1000.0, 1000.0));
	REQUIRE_FALSE(wallOverrunWithCpuBelowDeadline(1000.0, 600.0, 1000.0));
	REQUIRE_FALSE(wallOverrunWithCpuBelowDeadline(1200.0, -1.0, 1000.0));
	REQUIRE_FALSE(wallOverrunWithCpuBelowDeadline(1200.0, 600.0, 0.0));
}

TEST_CASE("Nonlinear TPT ladder remains silent and finite", "[audio-lab][kobber][ladder-candidate]")
{
	vekt::audio_lab::NonlinearTptLadder ladder;
	ladder.prepare(48'000.0);
	const vekt::audio_lab::NonlinearTptLadderSettings settings { 20'000.0f, 1.0f, 24.0f };
	for (int sample = 0; sample < 48'000; ++sample)
	{
		const auto output = ladder.process(0.0f, settings);
		REQUIRE(std::isfinite(output));
		REQUIRE(std::abs(output) <= 0.0f);
	}
	REQUIRE(ladder.diagnostics().nonFiniteSamples == 0);
	REQUIRE(ladder.diagnostics().unconvergedSamples == 0);
}

TEST_CASE("Coupled ladder solves the same four stage equations", "[audio-lab][kobber][ladder-coupled]")
{
	for (const auto rate : { 44'100.0, 48'000.0, 88'200.0, 96'000.0, 192'000.0 })
		for (const auto ceiling : { false, true })
			for (const auto resonance : { 0.0f, 0.98f, 1.0f })
				for (const auto drive : { 0.0f, 24.0f })
				{
					vekt::audio_lab::NonlinearTptLadder nested, coupled;
					vekt::audio_lab::NonlinearTptLadderOfflineReference reference;
					nested.prepare(rate);
					coupled.prepare(rate);
					reference.prepare(rate, 1);
					const auto cutoff = static_cast<float>(ceiling ? rate * 0.45 : 1'000.0);
					double largestDifference {};
					for (int sample = 0; sample < 512; ++sample)
					{
						const auto time = sample / rate;
						const auto input = sample < 128 ? 0.5f * static_cast<float>(std::sin(
							2.0 * std::numbers::pi * 7'000.0 * time)) : sample % 64 < 32 ? 4.0f : -4.0f;
						const auto cutoffNow = ceiling
							? cutoff - 500.0f * static_cast<float>(0.5 + 0.5 * std::sin(2.0 * std::numbers::pi * 37.0 * time))
							: cutoff + 600.0f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 37.0 * time));
						const auto resonanceNow = resonance == 0.0f ? 0.0f : std::clamp(resonance + 0.02f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 23.0 * time)), 0.0f, 1.0f);
						const auto driveNow = drive + 2.0f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 41.0 * time));
						const auto settings = vekt::audio_lab::NonlinearTptLadderSettings { cutoffNow, resonanceNow, driveNow };
						const auto actual = coupled.processCoupled(input, settings);
						const auto baseline = nested.process(input, settings);
						const auto expected = reference.process(input, { cutoffNow, resonanceNow, driveNow });
						largestDifference = std::max(largestDifference, std::abs(static_cast<double>(actual) - baseline));
						CAPTURE(rate, ceiling, resonance, drive, sample, actual, baseline, expected);
						REQUIRE(std::isfinite(actual));
						REQUIRE(std::abs(static_cast<double>(actual) - baseline) < 1.0e-4);
						REQUIRE(std::abs(static_cast<double>(actual) - expected) < 1.0e-4);
					}
					CAPTURE(rate, ceiling, resonance, drive, largestDifference,
						coupled.diagnostics().maximumResidual);
					REQUIRE(coupled.diagnostics().samples == 512);
					REQUIRE(coupled.diagnostics().unconvergedSamples == 0);
					REQUIRE(coupled.diagnostics().nonFiniteSamples == 0);
					REQUIRE(coupled.diagnostics().maximumResidual <= 2.0e-7f);
					REQUIRE(nested.diagnostics().unconvergedSamples == 0);
					REQUIRE(reference.diagnostics().unconvergedSteps == 0);
				}
}

TEST_CASE("Coupled and nested ladders agree on settled driven host-band components", "[audio-lab][kobber][ladder-coupled]")
{
	constexpr double rate = 48'000.0;
	constexpr int settle = 4'800, window = 4'800;
	for (const auto drive : { 12.0f, 24.0f })
	{
		vekt::audio_lab::NonlinearTptLadder nested, coupled;
		nested.prepare(rate);
		coupled.prepare(rate);
		std::array<std::vector<vekt::audio_analysis::SinusoidalProjector>, 2> bins;
		for (auto& path : bins)
			for (int hz = 1'000; hz < 24'000; hz += 1'000)
				path.emplace_back(rate, static_cast<double>(hz), window);
		double maximumDifference {}, sumDifferenceSquares {};
		for (int sample = 0; sample < settle + window; ++sample)
		{
			const auto input = 0.5f * static_cast<float>(std::sin(
				2.0 * std::numbers::pi * 7.0 * sample / 48.0));
			const vekt::audio_lab::NonlinearTptLadderSettings settings { 10'000.0f, 0.98f, drive };
			const auto reference = nested.process(input, settings);
			const auto actual = coupled.processCoupled(input, settings);
			if (sample >= settle)
			{
				const auto difference = static_cast<double>(actual) - reference;
				maximumDifference = std::max(maximumDifference, std::abs(difference));
				sumDifferenceSquares += difference * difference;
				for (auto& bin : bins[0]) bin.add(reference);
				for (auto& bin : bins[1]) bin.add(actual);
			}
		}
		const auto fundamental = bins[0][6].peakAmplitude();
		CAPTURE(drive, maximumDifference, fundamental,
			coupled.diagnostics().maximumResidual);
		REQUIRE(fundamental > 1.0e-5);
		REQUIRE(maximumDifference < 1.0e-4);
		REQUIRE(std::sqrt(sumDifferenceSquares / window) < 1.0e-5);
		for (std::size_t bin = 0; bin < bins[0].size(); ++bin)
		{
			const auto absoluteDifference = std::abs(bins[0][bin].peakAmplitude()
				- bins[1][bin].peakAmplitude());
			CAPTURE(bin, absoluteDifference);
			REQUIRE(absoluteDifference < 1.0e-5);
		}
		REQUIRE(nested.diagnostics().unconvergedSamples == 0);
		REQUIRE(coupled.diagnostics().unconvergedSamples == 0);
		REQUIRE(coupled.diagnostics().nonFiniteSamples == 0);
		REQUIRE(coupled.diagnostics().maximumResidual <= 2.0e-7f);
	}
}

TEST_CASE("Coupled ladder remains deterministic through abrupt controls and seeded input", "[audio-lab][kobber][ladder-coupled]")
{
	for (const auto rate : { 44'100.0, 48'000.0, 96'000.0, 192'000.0 })
	{
		vekt::audio_lab::NonlinearTptLadder coupled, rerun, nested;
		coupled.prepare(rate);
		rerun.prepare(rate);
		nested.prepare(rate);
		std::uint32_t random = 0x4d6f6e6fu;
		double maximumDifference {};
		for (int sample = 0; sample < 2'048; ++sample)
		{
			random = random * 1664525u + 1013904223u;
			const auto noise = (static_cast<float>(random >> 8) / 16777216.0f * 2.0f - 1.0f) * 4.0f;
			const auto input = sample % 257 == 0 ? 4.0f : sample % 257 == 1 ? -4.0f : noise;
			const auto cutoff = sample % 128 < 64 ? 10.0f : sample % 128 < 96 ? 1'000.0f
				: static_cast<float>(rate * 0.45);
			const auto resonance = sample % 64 < 32 ? 0.98f : 1.0f;
			const auto drive = sample % 96 < 48 ? 0.0f : 24.0f;
			const vekt::audio_lab::NonlinearTptLadderSettings settings { cutoff, resonance, drive };
			const auto actual = coupled.processCoupled(input, settings);
			const auto again = rerun.processCoupled(input, settings);
			const auto baseline = nested.process(input, settings);
			maximumDifference = std::max(maximumDifference, std::abs(static_cast<double>(actual) - baseline));
			CAPTURE(rate, sample, cutoff, resonance, drive, actual, baseline);
			REQUIRE(std::isfinite(actual));
			REQUIRE(std::bit_cast<std::uint32_t>(actual) == std::bit_cast<std::uint32_t>(again));
			REQUIRE(std::abs(static_cast<double>(actual) - baseline) < 1.0e-4);
		}
		CAPTURE(rate, maximumDifference, coupled.diagnostics().maximumResidual);
		REQUIRE(coupled.diagnostics().samples == 2'048);
		REQUIRE(coupled.diagnostics().unconvergedSamples == 0);
		REQUIRE(coupled.diagnostics().nonFiniteSamples == 0);
		REQUIRE(coupled.diagnostics().maximumResidual <= 2.0e-7f);
		REQUIRE(nested.diagnostics().unconvergedSamples == 0);
	}
}

TEST_CASE("Coupled ladder seeded resonance tail agrees with the nested solver", "[audio-lab][kobber][ladder-coupled][ladder-self-oscillation]")
{
	// Diagnostic regression across the sub-onset and above-onset regions;
	// the processor and listening gates remain separate.
	constexpr int sampleRate = 48'000;
	constexpr int excitationSamples = sampleRate / 10;
	constexpr int windowSamples = sampleRate / 10;
	for (const auto resonance : { 0.98f, 1.0f })
	{
		vekt::audio_lab::NonlinearTptLadder coupled, nested;
		coupled.prepare(sampleRate);
		nested.prepare(sampleRate);
		const vekt::audio_lab::NonlinearTptLadderSettings settings { 1'000.0f, resonance, 0.0f };
		// Without excitation both solvers must retain the exact zero equilibrium.
		for (int sample = 0; sample < 128; ++sample)
		{
			REQUIRE(std::abs(coupled.processCoupled(0.0f, settings)) <= 0.0f);
			REQUIRE(std::abs(nested.process(0.0f, settings)) <= 0.0f);
		}
		std::uint32_t random = 0x12345678u;
		double earlySquares {}, lateSquares {}, referenceLateSquares {};
		for (int sample = 0; sample < 2 * sampleRate + windowSamples; ++sample)
		{
			random = random * 1664525u + 1013904223u;
			const auto noise = (static_cast<float>(random >> 8) / 16777216.0f * 2.0f - 1.0f) * 0.05f;
			const auto input = sample < excitationSamples ? noise : 0.0f;
			const auto actual = coupled.processCoupled(input, settings);
			const auto reference = nested.process(input, settings);
			if (sample >= excitationSamples && sample < excitationSamples + windowSamples)
				earlySquares += static_cast<double>(actual) * actual;
			if (sample >= 2 * sampleRate && sample < 2 * sampleRate + windowSamples)
			{
				lateSquares += static_cast<double>(actual) * actual;
				referenceLateSquares += static_cast<double>(reference) * reference;
			}
		}
		const auto earlyRms = std::sqrt(earlySquares / windowSamples);
		const auto lateRms = std::sqrt(lateSquares / windowSamples);
		const auto referenceLateRms = std::sqrt(referenceLateSquares / windowSamples);
		INFO("resonance=" << resonance << ", early RMS=" << earlyRms
			<< ", late RMS=" << lateRms << ", nested late RMS=" << referenceLateRms);
		REQUIRE(std::abs(lateRms - referenceLateRms) < 0.002);
		REQUIRE(earlyRms > 0.001);
		if (resonance == 1.0f)
		{
			REQUIRE(lateRms > 0.05);
			REQUIRE(lateRms < 0.5);
		}
		else
		{
			REQUIRE(lateRms < earlyRms * 0.9);
			REQUIRE(lateRms < 1.0e-4);
		}
		REQUIRE(coupled.diagnostics().unconvergedSamples == 0);
		REQUIRE(coupled.diagnostics().nonFiniteSamples == 0);
		REQUIRE(nested.diagnostics().unconvergedSamples == 0);
	}
}

TEST_CASE("Coupled ladder grows into a stable tone above onset with a tighter reference", "[audio-lab][kobber][ladder-coupled][ladder-self-oscillation]")
{
	REQUIRE(vekt::kobber::ladderFeedbackGain(0.98) == Catch::Approx(3.92));
	REQUIRE(vekt::kobber::ladderFeedbackGain(1.0) == Catch::Approx(4.6));
	REQUIRE(vekt::kobber::ladderResonanceTuning(0.98) == Catch::Approx(1.0));
	REQUIRE(vekt::kobber::ladderResonanceTuning(1.0) == Catch::Approx(1.0287));
	constexpr int rate = 48'000;
	constexpr int burst = rate / 10;
	constexpr int window = rate / 10;
	std::array<double, 2> settled {};
	for (const auto amplitude : { 0.005f, 0.5f })
	{
		vekt::audio_lab::NonlinearTptLadder coupled;
		vekt::audio_lab::NonlinearTptLadderOfflineReference reference;
		coupled.prepare(rate);
		reference.prepare(rate, 1);
		constexpr vekt::audio_lab::NonlinearTptLadderSettings settings { 1'000.0f, 1.0f, 0.0f };
		constexpr vekt::audio_lab::NonlinearTptLadderReferenceSettings referenceSettings { 1'000.0, 1.0, 0.0 };
		// Above onset, zero is still an exact equilibrium until excited.
		for (int sample = 0; sample < 128; ++sample)
		{
			REQUIRE(std::abs(coupled.processCoupled(0.0f, settings)) <= 0.0f);
			REQUIRE(std::abs(reference.process(0.0, referenceSettings)) <= 0.0);
		}
		std::uint32_t random = 0x12345678u;
		double earlySquares {}, lateSquares {}, referenceSquares {}, latePeak {};
		int crossings {}, firstCrossing = -1, lastCrossing = -1;
		float previous {};
		for (int sample = 0; sample < 2 * rate + window; ++sample)
		{
			random = random * 1664525u + 1013904223u;
			const auto noise = (static_cast<float>(random >> 8) / 16777216.0f * 2.0f - 1.0f) * amplitude;
			const auto input = sample < burst ? noise : 0.0f;
			const auto actual = coupled.processCoupled(input, settings);
			const auto expected = reference.process(input, referenceSettings);
			if (sample >= burst && sample < burst + window)
				earlySquares += static_cast<double>(actual) * actual;
			if (sample >= 2 * rate)
			{
				lateSquares += static_cast<double>(actual) * actual;
				referenceSquares += expected * expected;
				latePeak = std::max(latePeak, std::abs(static_cast<double>(actual)));
				if (previous <= 0.0f && actual > 0.0f)
				{
					if (firstCrossing < 0) firstCrossing = sample;
					lastCrossing = sample;
					++crossings;
				}
			}
			previous = actual;
		}
		const auto earlyRms = std::sqrt(earlySquares / window);
		const auto lateRms = std::sqrt(lateSquares / window);
		const auto referenceRms = std::sqrt(referenceSquares / window);
		const auto frequency = crossings > 1
			? static_cast<double>(crossings - 1) * rate / (lastCrossing - firstCrossing) : 0.0;
		INFO("burst=" << amplitude << ", early=" << earlyRms << ", late=" << lateRms
			<< ", reference=" << referenceRms << ", pitch=" << frequency << ", peak=" << latePeak);
		REQUIRE(lateRms > 0.08);
		REQUIRE(lateRms < 0.25);
		REQUIRE(latePeak < 0.5);
		REQUIRE(frequency == Catch::Approx(1'000.0).margin(30.0));
		REQUIRE(std::abs(referenceRms - lateRms) < 0.005);
		REQUIRE(coupled.diagnostics().unconvergedSamples == 0);
		REQUIRE(coupled.diagnostics().nonFiniteSamples == 0);
		REQUIRE(reference.diagnostics().unconvergedSteps == 0);
		REQUIRE(reference.diagnostics().nonFiniteSteps == 0);
		settled[amplitude < 0.01f ? 0 : 1] = lateRms;
	}
	REQUIRE(std::abs(settled[0] - settled[1]) < 0.005);
}

TEST_CASE("Nonlinear TPT ladder drive compensation is an external output wrapper", "[audio-lab][kobber][ladder-candidate]")
{
	constexpr double sampleRate = 48'000.0;
	constexpr float driveDecibels = 12.0f;
	constexpr auto driveGain = 3.9810717055349722f;
	vekt::audio_lab::NonlinearTptLadder raw, compensated;
	raw.prepare(sampleRate);
	compensated.prepare(sampleRate);
	for (int sample = 0; sample < 12'000; ++sample)
	{
		const auto phase = 2.0 * std::numbers::pi * 500.0
			* static_cast<double>(sample) / sampleRate;
		const auto input = 0.25f * static_cast<float>(std::sin(phase));
		const auto rawOutput = raw.process(input, { 1'000.0f, 0.85f, driveDecibels, false });
		const auto compensatedOutput = compensated.process(input,
			{ 1'000.0f, 0.85f, driveDecibels, true });
		REQUIRE(compensatedOutput == Catch::Approx(rawOutput / std::sqrt(driveGain)).margin(1.0e-7));
	}
	REQUIRE(raw.diagnostics().unconvergedSamples == 0);
	REQUIRE(compensated.diagnostics().unconvergedSamples == 0);
}

TEST_CASE("Nonlinear TPT ladder analytical references preserve cutoff prewarping", "[audio-lab][kobber][ladder-reference]")
{
	for (const auto sampleRate : { 44'100.0, 48'000.0, 96'000.0 })
		for (const auto cutoff : { 250.0, 1'000.0, 4'000.0 })
		{
			const auto mapped = vekt::audio_lab::nonlinearTptLadderBilinearMappedResponse(
				sampleRate, cutoff, { cutoff, 0.0, 0.0 });
			const auto discrete = vekt::audio_lab::nonlinearTptLadderDiscreteResponse(
				sampleRate, cutoff, { cutoff, 0.0, 0.0 });
			INFO("sample rate=" << sampleRate << ", cutoff=" << cutoff);
			REQUIRE(std::abs(discrete) == Catch::Approx(std::abs(mapped)).margin(1.0e-12));
			REQUIRE(vekt::audio_analysis::wrapPhase(std::arg(discrete) - std::arg(mapped))
				== Catch::Approx(0.0).margin(1.0e-12));
		}
}

TEST_CASE("Offline reference one-step coefficient matches candidate at host cutoff ceiling", "[audio-lab][kobber][ladder-reference]")
{
	for (const auto rate : { 44'100.0, 48'000.0, 96'000.0 })
		for (const auto cutoff : { 1'000.0, rate * 0.45 })
		{
			vekt::audio_lab::NonlinearTptLadder candidate;
			vekt::audio_lab::NonlinearTptLadderOfflineReference reference;
			candidate.prepare(rate);
			reference.prepare(rate, 1);
			for (int sample = 0; sample < 2'048; ++sample)
			{
				const auto input = 0.5f * static_cast<float>(std::sin(
					2.0 * std::numbers::pi * 0.07 * sample));
				const auto actual = candidate.process(input, { static_cast<float>(cutoff), 0.5f, 12.0f });
				const auto expected = reference.process(input, { cutoff, 0.5, 12.0 });
				CAPTURE(rate, cutoff, sample, actual, expected);
				REQUIRE(static_cast<double>(actual) == Catch::Approx(expected).margin(1.0e-5));
			}
			REQUIRE(candidate.diagnostics().unconvergedSamples == 0);
			REQUIRE(reference.diagnostics().unconvergedSteps == 0);
		}
}

TEST_CASE("Nonlinear TPT ladder offline reference converges with smaller time steps", "[audio-lab][kobber][ladder-reference]")
{
	constexpr double sampleRate = 48'000.0;
	constexpr double frequency = 500.0;
	constexpr std::size_t samples = 12'000;
	const vekt::audio_lab::NonlinearTptLadderReferenceSettings settings { 1'000.0, 0.85, 6.0 };
	const auto render = [&](int substeps, double amplitude)
	{
		vekt::audio_lab::NonlinearTptLadderOfflineReference reference;
		reference.prepare(sampleRate, substeps);
		std::vector<double> output(samples);
		for (std::size_t sample = 0; sample < samples; ++sample)
		{
			const auto phase = 2.0 * std::numbers::pi * frequency
				* static_cast<double>(sample) / sampleRate;
			output[sample] = reference.process(amplitude * std::sin(phase), settings);
		}
		REQUIRE(reference.diagnostics().unconvergedSteps == 0);
		REQUIRE(reference.diagnostics().nonFiniteSteps == 0);
		return output;
	};
	for (const auto amplitude : { 0.001, 0.5, 1.0 })
	{
		const auto factor4 = render(4, amplitude);
		const auto factor8 = render(8, amplitude);
		const auto factor16 = render(16, amplitude);
		const auto difference4To8 = vekt::audio_analysis::compareSamples<double, double>(factor4, factor8);
		const auto difference8To16 = vekt::audio_analysis::compareSamples<double, double>(factor8, factor16);
		INFO("amplitude=" << amplitude << ", 4x->8x rms=" << difference4To8.rmsDifference
			<< ", 8x->16x rms=" << difference8To16.rmsDifference);
		REQUIRE(difference8To16.rmsDifference < difference4To8.rmsDifference * 0.4);
		REQUIRE(difference8To16.maximumAbsoluteDifference
			< difference4To8.maximumAbsoluteDifference * 0.4);
	}
}

TEST_CASE("Offline reference overload remains converged beyond the feedback state clamp", "[audio-lab][kobber][ladder-reference]")
{
	// A driven alternating signal requires feedback iterates outside [-24, 24].
	// Even at the host cutoff ceiling the same prewarped pole uses gHost / N.
	for (const auto sampleRate : { 44'100.0, 48'000.0, 88'200.0, 96'000.0, 192'000.0 })
		for (const auto cutoff : { 10.0, 1'000.0, sampleRate * 0.45 })
			for (const auto resonance : { 0.5, 1.0 })
			{
				vekt::audio_lab::NonlinearTptLadderOfflineReference reference;
				reference.prepare(sampleRate, 16);
				double maximumOutput {};
				for (int sample = 0; sample < 512; ++sample)
				{
					const auto output = reference.process(sample % 2 ? 4.0 : -4.0,
						{ cutoff, resonance, 24.0 });
					REQUIRE(std::isfinite(output));
					maximumOutput = std::max(maximumOutput, std::abs(output));
				}
				CAPTURE(sampleRate, cutoff, resonance, maximumOutput,
					reference.diagnostics().maximumResidual);
				REQUIRE(maximumOutput <= 24.0);
				REQUIRE(reference.diagnostics().internalSteps == 512 * 16);
				REQUIRE(reference.diagnostics().unconvergedSteps == 0);
				REQUIRE(reference.diagnostics().nonFiniteSteps == 0);
				REQUIRE(reference.diagnostics().maximumResidual <= 1.0e-13);
			}
}

TEST_CASE("Offline reference high-cutoff overload needs progressive substep convergence", "[audio-lab][kobber][ladder-reference][slow]")
{
	constexpr double rate = 48'000.0;
	constexpr int samples = 2'048;
	std::array<std::vector<double>, 5> output;
	for (std::size_t path = 0; path < output.size(); ++path)
	{
		const auto substeps = 16 << path;
		vekt::audio_lab::NonlinearTptLadderOfflineReference reference;
		reference.prepare(rate, substeps);
		output[path].resize(samples);
		for (int sample = 0; sample < samples; ++sample)
		{
			const auto input = 4.0 * std::sin(2.0 * std::numbers::pi * 0.07 * sample);
			output[path][static_cast<std::size_t>(sample)] = reference.process(input,
				{ rate * 0.45, 0.85, 12.0 });
		}
		CAPTURE(substeps, reference.diagnostics().maximumResidual);
		REQUIRE(reference.diagnostics().unconvergedSteps == 0);
		REQUIRE(reference.diagnostics().nonFiniteSteps == 0);
		REQUIRE(reference.diagnostics().maximumResidual <= 1.0e-13);
	}
	const auto first = vekt::audio_analysis::compareSamples<double, double>(
		std::span(output[0]).subspan(samples / 2), std::span(output[1]).subspan(samples / 2));
	const auto second = vekt::audio_analysis::compareSamples<double, double>(
		std::span(output[1]).subspan(samples / 2), std::span(output[2]).subspan(samples / 2));
	const auto third = vekt::audio_analysis::compareSamples<double, double>(
		std::span(output[2]).subspan(samples / 2), std::span(output[3]).subspan(samples / 2));
	const auto fourth = vekt::audio_analysis::compareSamples<double, double>(
		std::span(output[3]).subspan(samples / 2), std::span(output[4]).subspan(samples / 2));
	INFO("16->32 RMS=" << first.rmsDifference << ", 32->64 RMS=" << second.rmsDifference
		<< ", 64->128 RMS=" << third.rmsDifference << ", 128->256 RMS=" << fourth.rmsDifference);
	REQUIRE(first.rmsDifference > 0.005);
	REQUIRE(second.rmsDifference < first.rmsDifference * 0.4);
	REQUIRE(second.rmsDifference > 0.001);
	REQUIRE(third.rmsDifference < second.rmsDifference * 0.4);
	REQUIRE(fourth.rmsDifference < third.rmsDifference * 0.4);
	REQUIRE(fourth.rmsDifference > 1.0e-5);
}

TEST_CASE("Nonlinear TPT ladder offline reference rejects altered model scaling", "[audio-lab][kobber][ladder-reference]")
{
	constexpr double sampleRate = 48'000.0;
	constexpr double frequency = 500.0;
	constexpr double amplitude = 0.5;
	constexpr std::size_t samples = 12'000;
	const vekt::audio_lab::NonlinearTptLadderSettings candidateSettings { 1'000.0f, 0.85f, 6.0f };
	const vekt::audio_lab::NonlinearTptLadderReferenceSettings referenceSettings { 1'000.0, 0.85, 6.0 };
	vekt::audio_lab::NonlinearTptLadder candidate;
	vekt::audio_lab::NonlinearTptLadderOfflineReference reference, altered;
	candidate.prepare(sampleRate);
	reference.prepare(sampleRate, 16);
	altered.prepare(sampleRate, 16);
	std::vector<double> candidateOutput(samples), referenceOutput(samples), alteredOutput(samples);
	for (std::size_t sample = 0; sample < samples; ++sample)
	{
		const auto phase = 2.0 * std::numbers::pi * frequency
			* static_cast<double>(sample) / sampleRate;
		const auto input = amplitude * std::sin(phase);
		candidateOutput[sample] = candidate.process(static_cast<float>(input), candidateSettings);
		referenceOutput[sample] = reference.process(input, referenceSettings);
		alteredOutput[sample] = altered.process(input, { 1'300.0, 0.65, 6.0 });
	}
	const auto settledCandidate = std::span(candidateOutput).subspan(6'000);
	const auto settledReference = std::span(referenceOutput).subspan(6'000);
	const auto settledAltered = std::span(alteredOutput).subspan(6'000);
	const auto correctDifference = vekt::audio_analysis::compareSamples<double, double>(
		settledCandidate, settledReference);
	const auto alteredDifference = vekt::audio_analysis::compareSamples<double, double>(
		settledCandidate, settledAltered);
	INFO("correct rms=" << correctDifference.rmsDifference
		<< ", altered rms=" << alteredDifference.rmsDifference);
	REQUIRE(reference.diagnostics().unconvergedSteps == 0);
	REQUIRE(altered.diagnostics().unconvergedSteps == 0);
	REQUIRE(correctDifference.rmsDifference < alteredDifference.rmsDifference * 0.25);
}

TEST_CASE("Nonlinear TPT ladder offline reference converges under control modulation", "[audio-lab][kobber][ladder-reference]")
{
	constexpr double sampleRate = 48'000.0;
	constexpr std::size_t samples = 12'000;
	const auto render = [&](int substeps)
	{
		vekt::audio_lab::NonlinearTptLadderOfflineReference reference;
		reference.prepare(sampleRate, substeps);
		std::vector<double> output(samples);
		for (std::size_t sample = 0; sample < samples; ++sample)
		{
			const auto time = static_cast<double>(sample) / sampleRate;
			const auto input = 0.6 * std::sin(2.0 * std::numbers::pi * 220.0 * time);
			const auto cutoff = 2'150.0 + 1'850.0
				* std::sin(2.0 * std::numbers::pi * 37.0 * time);
			const auto resonance = 0.55 + 0.35
				* std::sin(2.0 * std::numbers::pi * 23.0 * time + 0.4);
			output[sample] = reference.process(input, { cutoff, resonance, 9.0 });
		}
		REQUIRE(reference.diagnostics().unconvergedSteps == 0);
		REQUIRE(reference.diagnostics().nonFiniteSteps == 0);
		return output;
	};
	const auto factor4 = render(4);
	const auto factor8 = render(8);
	const auto factor16 = render(16);
	const auto difference4To8 = vekt::audio_analysis::compareSamples<double, double>(factor4, factor8);
	const auto difference8To16 = vekt::audio_analysis::compareSamples<double, double>(factor8, factor16);
	INFO("modulated 4x->8x rms=" << difference4To8.rmsDifference
		<< ", 8x->16x rms=" << difference8To16.rmsDifference);
	REQUIRE(difference8To16.rmsDifference < difference4To8.rmsDifference * 0.4);
	REQUIRE(difference8To16.maximumAbsoluteDifference
		< difference4To8.maximumAbsoluteDifference * 0.4);
}

TEST_CASE("Ladder prototype report is deterministic and block-size invariant", "[audio-lab][kobber][ladder-candidate][determinism][slow]")
{
	const auto first = vekt::audio_lab::renderLadderPrototype(24'000.0, 31);
	const auto second = vekt::audio_lab::renderLadderPrototype(24'000.0, 257);
	REQUIRE(first.audio.getNumSamples() == second.audio.getNumSamples());
	for (int channel = 0; channel < first.audio.getNumChannels(); ++channel)
		for (int sample = 0; sample < first.audio.getNumSamples(); ++sample)
			REQUIRE(std::bit_cast<std::uint32_t>(first.audio.getSample(channel, sample))
				== std::bit_cast<std::uint32_t>(second.audio.getSample(channel, sample)));
	REQUIRE(first.report.getProperty("frequency_response", {}).size() == 8);
	const auto contract = first.report.getProperty("validation_contract", {});
	REQUIRE(contract.getProperty("product_direction", {}).toString()
		== "hybrid-classic-ladder-modern-features");
	REQUIRE(contract.getProperty("ladder_output", {}).toString() == "raw-fourth-stage");
	REQUIRE(static_cast<double>(contract.getProperty("maximum_resonance_feedback", 0.0)) == Catch::Approx(4.6));
	REQUIRE(contract.getProperty("release_validation_status", {}).toString() == "open");
	REQUIRE(first.report.getProperty("planned_validation_matrix", {})
		.getProperty("completion", {}).toString() == "not-complete");
	REQUIRE(first.report.getProperty("provisional_acceptance_limits", {})
		.getProperty("status", {}).toString() == "provisional-not-adr-acceptance");
	const auto* response = first.report.getProperty("frequency_response", {}).getArray();
	REQUIRE(response != nullptr);
	REQUIRE(static_cast<double>((*response)[0].getProperty("gain", 0.0))
		> static_cast<double>((*response)[7].getProperty("gain", 0.0)));
	REQUIRE(std::abs(static_cast<double>((*response)[4].getProperty("phase_radians", 0.0))) > 1.0);
	REQUIRE(std::abs(static_cast<double>((*response)[4]
		.getProperty("measured_minus_analytical_phase_radians", 1.0))) < 0.001);
	for (std::size_t index = 0; index < static_cast<std::size_t>(response->size()); ++index)
	{
		const auto& point = (*response)[static_cast<int>(index)];
		const auto gainError = std::abs(static_cast<double>(point
			.getProperty("measured_minus_analytical_gain_db", 1.0)));
		const auto phaseError = std::abs(static_cast<double>(point
			.getProperty("measured_minus_analytical_phase_radians", 1.0)));
		INFO("frequency=" << static_cast<double>(point.getProperty("frequency_hz", 0.0)));
		REQUIRE(gainError < (index < 6 ? 0.01 : 1.0));
		REQUIRE(phaseError < (index < 6 ? 0.001 : 0.025));
	}
	const auto calibration = first.report.getProperty("cutoff_calibration", {});
	REQUIRE(static_cast<double>(calibration.getProperty("measured_minus_3_db_hz", 0.0)) > 250.0);
	REQUIRE(static_cast<double>(calibration.getProperty("measured_minus_3_db_hz", 0.0)) < 750.0);
	const auto* ringdown = first.report.getProperty("ringdown", {}).getArray();
	REQUIRE(ringdown != nullptr);
	REQUIRE(static_cast<int>((*ringdown)[0].getProperty("extinction_sample", 0)) > 0);
	REQUIRE(static_cast<double>((*ringdown)[0].getProperty("estimated_frequency_hz", 0.0)) > 500.0);
	requireConverged(first.report.getProperty("frequency_response", {}));
	requireConverged(first.report.getProperty("level_response", {}));
	requireConverged(first.report.getProperty("ringdown", {}));
	const auto firstModels = first.report.getProperty("models", {});
	const auto secondModels = second.report.getProperty("models", {});
	REQUIRE(juce::JSON::toString(firstModels, true) == juce::JSON::toString(secondModels, true));
	const auto current = firstModels.getProperty("current_delayed_feedback", {});
	const auto candidate = firstModels.getProperty("candidate_nonlinear_tpt", {});
	REQUIRE(current.isObject());
	REQUIRE(candidate.isObject());
	requireFinite(current.getProperty("frequency_response", {}));
	requireFinite(current.getProperty("level_response", {}));
	requireFinite(current.getProperty("ringdown", {}));
	REQUIRE(static_cast<double>(current.getProperty("cutoff_calibration", {})
		.getProperty("measured_minus_3_db_hz", 0.0)) > 250.0);
	const auto* currentRingdown = current.getProperty("ringdown", {}).getArray();
	REQUIRE(currentRingdown != nullptr);
	REQUIRE_FALSE(static_cast<bool>((*currentRingdown)[0].getProperty("extinguished", true)));
	REQUIRE(static_cast<double>((*currentRingdown)[0].getProperty("estimated_frequency_hz", 0.0)) > 900.0);
	const auto comparison = first.report.getProperty("comparison", {});
	REQUIRE(comparison.isObject());
	REQUIRE(juce::JSON::toString(comparison, true)
		== juce::JSON::toString(second.report.getProperty("comparison", {}), true));
	const auto normalization = comparison.getProperty("low_level_output_normalization", {});
	REQUIRE(std::abs(static_cast<double>(normalization.getProperty("reference_frequency_hz", 0.0))
		- 125.0) < 1.0e-9);
	REQUIRE(static_cast<double>(normalization.getProperty("candidate_output_scale", 0.0)) > 0.0);
	const auto* frequencyDeltas = comparison.getProperty("frequency_response_delta", {}).getArray();
	REQUIRE(frequencyDeltas != nullptr);
	REQUIRE(frequencyDeltas->size() == 8);
	for (const auto& delta : *frequencyDeltas)
	{
		REQUIRE(std::isfinite(static_cast<double>(delta.getProperty("candidate_minus_current_gain_db", 0.0))));
		REQUIRE(std::isfinite(static_cast<double>(delta.getProperty("candidate_minus_current_phase_radians", 0.0))));
		REQUIRE(std::isfinite(static_cast<double>(delta.getProperty("normalized_candidate_minus_current_gain_db", 0.0))));
	}
	REQUIRE(std::abs(static_cast<double>((*frequencyDeltas)[0]
		.getProperty("normalized_candidate_minus_current_gain_db", 1.0))) < 1.0e-9);
	const auto* levelDeltas = comparison.getProperty("level_response_delta", {}).getArray();
	REQUIRE(levelDeltas != nullptr);
	REQUIRE(levelDeltas->size() == 4);
	for (const auto& delta : *levelDeltas)
	{
		REQUIRE(std::isfinite(static_cast<double>(delta
			.getProperty("candidate_minus_current_fundamental_gain_db", 0.0))));
		REQUIRE(std::isfinite(static_cast<double>(delta
			.getProperty("normalized_candidate_minus_current_fundamental_gain_db", 0.0))));
	}
	const auto* candidateLevels = candidate.getProperty("level_response", {}).getArray();
	REQUIRE(candidateLevels != nullptr);
	REQUIRE(candidateLevels->size() == 4);
	for (const auto& level : *candidateLevels)
	{
		const auto fundamentalError = std::abs(static_cast<double>(level
			.getProperty("candidate_minus_offline_reference_fundamental_gain_db", 1.0)));
		const auto secondHarmonicError = std::abs(static_cast<double>(level
			.getProperty("candidate_minus_offline_reference_second_harmonic", 1.0)));
		const auto thirdHarmonicError = std::abs(static_cast<double>(level
			.getProperty("candidate_minus_offline_reference_third_harmonic", 1.0)));
		REQUIRE(fundamentalError < 0.03);
		REQUIRE(secondHarmonicError < 1.0e-5);
		REQUIRE(thirdHarmonicError < 8.0e-4);
		const auto reference = level.getProperty("offline_reference", {});
		REQUIRE(static_cast<int>(reference.getProperty("reference_substeps", 0)) == 16);
		REQUIRE(static_cast<juce::int64>(reference.getProperty("unconverged_steps", -1)) == 0);
		REQUIRE(static_cast<juce::int64>(reference.getProperty("non_finite_steps", -1)) == 0);
		REQUIRE(static_cast<double>(reference.getProperty("maximum_residual", 1.0)) <= 1.0e-13);
	}
	const auto* ringdownDeltas = comparison.getProperty("ringdown_delta", {}).getArray();
	REQUIRE(ringdownDeltas != nullptr);
	REQUIRE(ringdownDeltas->size() == 2);
	for (const auto& delta : *ringdownDeltas)
	{
		REQUIRE(std::isfinite(static_cast<double>(delta.getProperty("candidate_minus_current_peak_db", 0.0))));
		REQUIRE(std::isfinite(static_cast<double>(delta.getProperty("candidate_minus_current_frequency_hz", 0.0))));
	}
	REQUIRE_FALSE(static_cast<bool>((*ringdownDeltas)[0].getProperty("current_extinguished", true)));
	REQUIRE(static_cast<bool>((*ringdownDeltas)[0].getProperty("candidate_extinguished", false)));
	const auto firstMapping = first.report.getProperty("cutoff_mapping", {});
	const auto secondMapping = second.report.getProperty("cutoff_mapping", {});
	REQUIRE(firstMapping.isObject());
	REQUIRE(juce::JSON::toString(firstMapping, true) == juce::JSON::toString(secondMapping, true));
	REQUIRE_FALSE(static_cast<bool>(firstMapping.getProperty("constant_scale_is_adequate", true)));
	REQUIRE(static_cast<double>(firstMapping.getProperty("point_scale_spread_cents", 0.0)) > 100.0);
	REQUIRE(static_cast<double>(firstMapping
		.getProperty("maximum_absolute_constant_scale_residual_cents", 0.0)) > 100.0);
	const auto twoTone = first.report.getProperty("two_tone_imd", {});
	REQUIRE(static_cast<double>(twoTone.getProperty("first_fundamental", 0.0)) > 0.0);
	REQUIRE(static_cast<double>(twoTone.getProperty("second_fundamental", 0.0)) > 0.0);
	REQUIRE(static_cast<juce::int64>(twoTone.getProperty("solver", {})
		.getProperty("unconverged_samples", -1)) == 0);
	const auto* stopband = first.report.getProperty("stopband_tolerance", {})
		.getProperty("points", {}).getArray();
	REQUIRE(stopband != nullptr);
	REQUIRE(stopband->size() == 3);
	for (const auto& point : *stopband)
	{
		REQUIRE(std::isfinite(static_cast<double>(point.getProperty("relative_gain_error_db", 0.0))));
		REQUIRE(static_cast<double>(point.getProperty("absolute_output_peak_error", -1.0)) >= 0.0);
		REQUIRE(static_cast<juce::int64>(point.getProperty("solver", {})
			.getProperty("unconverged_samples", -1)) == 0);
	}
	const auto* rates = firstMapping.getProperty("sample_rates", {}).getArray();
	REQUIRE(rates != nullptr);
	REQUIRE(rates->size() == 3);
	for (const auto& rate : *rates)
	{
		const auto* points = rate.getProperty("points", {}).getArray();
		REQUIRE(points != nullptr);
		REQUIRE(points->size() == 3);
		double previousCurrent {}, previousCandidate {};
		for (const auto& point : *points)
		{
			const auto currentHz = static_cast<double>(point
				.getProperty("current_measured_minus_3_db_hz", 0.0));
			const auto candidateHz = static_cast<double>(point
				.getProperty("candidate_measured_minus_3_db_hz", 0.0));
			REQUIRE(std::isfinite(currentHz));
			REQUIRE(std::isfinite(candidateHz));
			REQUIRE(currentHz > previousCurrent);
			REQUIRE(candidateHz > previousCandidate);
			REQUIRE(std::isfinite(static_cast<double>(point
				.getProperty("proportional_candidate_control_scale_estimate", 0.0))));
			REQUIRE(std::isfinite(static_cast<double>(point
				.getProperty("constant_scale_residual_cents", 0.0))));
			previousCurrent = currentHz;
			previousCandidate = candidateHz;
		}
	}
}

TEST_CASE("Nonlinear TPT ladder supported-range solver matrix is bounded and deterministic", "[audio-lab][kobber][ladder-matrix][slow]")
{
	constexpr std::array sampleRates { 44'100.0, 48'000.0, 88'200.0, 96'000.0, 192'000.0 };
	constexpr std::array resonances { 0.0f, 0.5f, 0.85f, 0.95f, 0.98f, 1.0f };
	constexpr std::array amplitudes { 0.000001f, 0.001f, 0.05f, 0.5f, 1.0f, 4.0f };
	constexpr std::array drives { 0.0f, 6.0f, 12.0f, 24.0f };
	for (const auto sampleRate : sampleRates)
	{
		const std::array cutoffs { 10.0f, 100.0f, 1'000.0f,
			static_cast<float>(sampleRate * 0.25), static_cast<float>(sampleRate * 0.45) };
		for (const auto cutoff : cutoffs)
			for (const auto resonance : resonances)
				for (const auto amplitude : amplitudes)
					for (const auto drive : drives)
					{
						CAPTURE(sampleRate, cutoff, resonance, amplitude, drive);
						vekt::audio_lab::NonlinearTptLadder first, second;
						first.prepare(sampleRate);
						second.prepare(sampleRate);
						for (int sample = 0; sample < 256; ++sample)
						{
							const auto time = static_cast<double>(sample) / sampleRate;
							const auto excitation = sample == 0 ? amplitude
								: amplitude * static_cast<float>(0.6 * std::sin(2.0 * std::numbers::pi * 173.0 * time)
									+ 0.4 * std::sin(2.0 * std::numbers::pi * 431.0 * time));
							const vekt::audio_lab::NonlinearTptLadderSettings settings {
								cutoff, resonance, drive, false };
							const auto firstOutput = first.process(excitation, settings);
							const auto secondOutput = second.process(excitation, settings);
							INFO("rate=" << sampleRate << ", cutoff=" << cutoff
								<< ", resonance=" << resonance << ", amplitude=" << amplitude
								<< ", drive=" << drive << ", sample=" << sample);
							REQUIRE(std::isfinite(firstOutput));
							REQUIRE(std::abs(firstOutput) <= 24.0f);
							REQUIRE(std::bit_cast<std::uint32_t>(firstOutput)
								== std::bit_cast<std::uint32_t>(secondOutput));
						}
						CAPTURE(first.diagnostics().unconvergedSamples,
							first.diagnostics().maximumResidual,
							first.diagnostics().maximumFeedbackIterations,
							first.diagnostics().maximumStageIterations);
						REQUIRE(first.diagnostics().unconvergedSamples == 0);
						REQUIRE(first.diagnostics().nonFiniteSamples == 0);
						REQUIRE(first.diagnostics().maximumFeedbackIterations <= 16);
						REQUIRE(first.diagnostics().maximumStageIterations <= 24);
						REQUIRE(first.diagnostics().maximumResidual <= 2.0e-7f);
					}
	}
}

TEST_CASE("Nonlinear TPT ladder modulation remains deterministic across block sizes", "[audio-lab][kobber][ladder-modulation]")
{
	constexpr double sampleRate = 48'000.0;
	constexpr int sampleCount = 4'096;
	constexpr std::array blockSizes { 1, 16, 31, 32, 127, 128, 257 };
	for (int mode = 0; mode < 4; ++mode)
	{
		const auto render = [&](int blockSize)
		{
			vekt::audio_lab::NonlinearTptLadder ladder;
			ladder.prepare(sampleRate);
			std::vector<float> output(sampleCount);
			for (int start = 0; start < sampleCount; start += blockSize)
				for (int sample = start; sample < std::min(start + blockSize, sampleCount); ++sample)
				{
					const auto time = static_cast<double>(sample) / sampleRate;
					const auto input = 0.4f * static_cast<float>(
						std::sin(2.0 * std::numbers::pi * 220.0 * time));
					const auto cutoff = 1'200.0f + ((mode == 0 || mode == 3) ? 800.0f
						* static_cast<float>(std::sin(2.0 * std::numbers::pi * 37.0 * time)) : 0.0f);
					const auto resonance = 0.55f + ((mode == 1 || mode == 3) ? 0.3f
						* static_cast<float>(std::sin(2.0 * std::numbers::pi * 23.0 * time)) : 0.0f);
					const auto drive = 6.0f + ((mode == 2 || mode == 3) ? 6.0f
						* static_cast<float>(std::sin(2.0 * std::numbers::pi * 41.0 * time)) : 0.0f);
					output[static_cast<std::size_t>(sample)] = ladder.process(input,
						{ cutoff, resonance, drive });
				}
			REQUIRE(ladder.diagnostics().nonFiniteSamples == 0);
			CAPTURE(mode, blockSize, ladder.diagnostics().maximumResidual);
			REQUIRE(ladder.diagnostics().unconvergedSamples == 0);
			REQUIRE(ladder.diagnostics().maximumResidual <= 2.0e-7f);
			return output;
		};
		const auto reference = render(blockSizes[0]);
		for (const auto blockSize : blockSizes)
		{
			const auto output = render(blockSize);
			for (int sample = 0; sample < sampleCount; ++sample)
			{
				CAPTURE(mode, blockSize, sample);
				REQUIRE(std::isfinite(output[static_cast<std::size_t>(sample)]));
				REQUIRE(std::bit_cast<std::uint32_t>(output[static_cast<std::size_t>(sample)])
					== std::bit_cast<std::uint32_t>(reference[static_cast<std::size_t>(sample)]));
			}
		}
	}
}

TEST_CASE("Nonlinear TPT ladder resonance boundary has measured impulse ringdown", "[audio-lab][kobber][ladder-self-oscillation][slow]")
{
	for (const auto sampleRate : { 44'100.0, 48'000.0, 88'200.0, 96'000.0, 192'000.0 })
	for (const auto resonance : { 0.98f, 1.0f })
	{
		const auto sampleCount = static_cast<int>(sampleRate * 2.0);
		vekt::audio_lab::NonlinearTptLadder ladder;
		ladder.prepare(sampleRate);
		double initialPeak {}, finalPeak {}, finalSquareSum {};
		int finalCrossings {}, firstCrossing = -1, lastCrossing = -1;
		float previous {};
		for (int sample = 0; sample < sampleCount; ++sample)
		{
			const auto output = ladder.process(sample == 0 ? 1.0f : 0.0f,
				{ 1'000.0f, resonance, 0.0f });
			if (sample < static_cast<int>(sampleRate * 0.1))
				initialPeak = std::max(initialPeak, std::abs(static_cast<double>(output)));
			if (sample >= sampleCount - static_cast<int>(sampleRate * 0.5))
			{
				finalPeak = std::max(finalPeak, std::abs(static_cast<double>(output)));
				finalSquareSum += static_cast<double>(output) * output;
				if (previous <= 0.0f && output > 0.0f)
				{
					if (firstCrossing < 0) firstCrossing = sample;
					lastCrossing = sample;
					++finalCrossings;
				}
			}
			previous = output;
		}
		const auto finalRms = std::sqrt(finalSquareSum / (sampleRate * 0.5));
		const auto tailFrequency = finalCrossings > 1
			? (finalCrossings - 1) * sampleRate / (lastCrossing - firstCrossing) : 0.0;
		CAPTURE(sampleRate, resonance, initialPeak, finalPeak, finalRms, tailFrequency, finalCrossings,
			ladder.diagnostics().unconvergedSamples, ladder.diagnostics().maximumResidual);
		REQUIRE(ladder.diagnostics().nonFiniteSamples == 0);
		REQUIRE(ladder.diagnostics().unconvergedSamples == 0);
		REQUIRE(initialPeak > 1.0e-5);
		if (resonance < 1.0f) REQUIRE(finalPeak < 1.0e-5);
		else
		{
			REQUIRE(finalPeak > 1.0e-5);
			REQUIRE(finalRms > 1.0e-5);
			REQUIRE(finalCrossings > 100);
			REQUIRE(tailFrequency > 100.0);
			REQUIRE(tailFrequency < 2'000.0);
		}
	}
}

TEST_CASE("Nonlinear TPT ladder cutoff-boundary ringdown is finite and measured", "[audio-lab][kobber][ladder-self-oscillation][slow]")
{
	for (const auto rate : { 44'100.0, 48'000.0, 88'200.0, 96'000.0, 192'000.0 })
		for (const auto cutoff : { 10.0f, static_cast<float>(rate * 0.45) })
			for (const auto resonance : { 0.98f, 1.0f })
			{
				vekt::audio_lab::NonlinearTptLadder ladder;
				ladder.prepare(rate);
				// At 10 Hz, a half-second startup-adjacent window has only five
				// cycles and is biased by the impulse transient. Measure five
				// seconds after settling; retain the 1% pitch requirement.
				const auto length = static_cast<int>(rate * (cutoff == 10.0f ? 10.0 : 2.0));
				const auto tailLength = static_cast<int>(rate * (cutoff == 10.0f ? 5.0 : 0.5));
				double peak {}, squares {};
				int crossings {}, firstCrossing = -1, lastCrossing = -1;
				float previous {};
				for (int sample = 0; sample < length; ++sample)
				{
					const auto value = ladder.process(sample == 0 ? 1.0f : 0.0f,
						{ cutoff, resonance, 0.0f });
					if (sample >= length - tailLength)
					{
						peak = std::max(peak, std::abs(static_cast<double>(value)));
						squares += static_cast<double>(value) * value;
						if (previous <= 0.0f && value > 0.0f)
						{
							if (firstCrossing < 0) firstCrossing = sample;
							lastCrossing = sample;
							++crossings;
						}
					}
					previous = value;
				}
				const auto rms = std::sqrt(squares / tailLength);
				const auto frequency = crossings > 1
					? (crossings - 1) * rate / (lastCrossing - firstCrossing) : 0.0;
				CAPTURE(rate, cutoff, resonance, peak, rms, crossings, frequency,
					ladder.diagnostics().maximumResidual);
				REQUIRE(ladder.diagnostics().unconvergedSamples == 0);
				REQUIRE(ladder.diagnostics().nonFiniteSamples == 0);
				REQUIRE(ladder.diagnostics().maximumResidual <= 2.0e-7f);
				REQUIRE(std::isfinite(peak));
				REQUIRE(std::isfinite(rms));
				if (resonance == 1.0f)
				{
					REQUIRE(peak > 0.0);
					REQUIRE(rms > 0.0);
					REQUIRE(crossings > 1);
					REQUIRE(std::abs(frequency / cutoff - 1.0) < 0.01);
				}
				// The proposed 1 kHz ringdown amplitude limits do not hold at 10 Hz.
				// Keep those floor-cutoff outcomes visible in the validation record,
				// rather than applying the 1 kHz acceptance policy to other cutoffs.
			}
}

TEST_CASE("Nonlinear TPT ladder 10 Hz floor has distinct 30-second ringdown targets", "[audio-lab][kobber][ladder-floor][slow]")
{
	// Development-only proposed floor targets. The 1 kHz two-second policy is
	// intentionally not applied to a five-cycle, half-second 10 Hz window.
	for (const auto rate : { 44'100.0, 48'000.0, 88'200.0, 96'000.0, 192'000.0 })
		for (const auto resonance : { 0.98f, 1.0f })
		{
			const auto windowSamples = static_cast<int>(rate * 5.0);
			const auto renderSamples = windowSamples * 6;
			vekt::audio_lab::NonlinearTptLadder ladder;
			ladder.prepare(rate);
			vekt::audio_analysis::SampleStatisticsAccumulator early, late;
			int firstAboveThreshold = -1;
			int crossings {}, firstCrossing = -1, lastCrossing = -1;
			float previous {};
			for (int sample = 0; sample < renderSamples; ++sample)
			{
				const auto value = ladder.process(sample == 0 ? 1.0f : 0.0f,
					{ 10.0f, resonance, 0.0f });
				if (firstAboveThreshold < 0 && std::abs(value) >= 1.0e-5f)
					firstAboveThreshold = sample;
				if (sample >= windowSamples && sample < 2 * windowSamples)
					early.add(value);
				if (sample >= renderSamples - windowSamples)
				{
					late.add(value);
					if (previous <= 0.0f && value > 0.0f)
					{
						if (firstCrossing < 0) firstCrossing = sample;
						lastCrossing = sample;
						++crossings;
					}
				}
				previous = value;
			}
			const auto earlyStats = early.result();
			const auto lateStats = late.result();
			const auto tailFrequency = crossings > 1
				? (crossings - 1) * rate / (lastCrossing - firstCrossing) : 0.0;
			CAPTURE(rate, resonance, firstAboveThreshold, earlyStats.peak, earlyStats.rms,
				lateStats.peak, lateStats.rms, crossings, tailFrequency,
				ladder.diagnostics().maximumResidual);
			REQUIRE(firstAboveThreshold >= 0);
			REQUIRE(static_cast<double>(firstAboveThreshold) / rate < 0.02);
			REQUIRE(earlyStats.samples == static_cast<std::size_t>(windowSamples));
			REQUIRE(lateStats.samples == static_cast<std::size_t>(windowSamples));
			REQUIRE(ladder.diagnostics().samples == static_cast<std::uint64_t>(renderSamples));
			REQUIRE(ladder.diagnostics().nonFiniteSamples == 0);
			REQUIRE(ladder.diagnostics().unconvergedSamples == 0);
			REQUIRE(ladder.diagnostics().maximumResidual <= 2.0e-7f);
			REQUIRE(std::isfinite(lateStats.peak));
			REQUIRE(std::isfinite(lateStats.rms));
			if (resonance < 1.0f)
			{
				REQUIRE(lateStats.peak < 2.0e-6);
				REQUIRE(lateStats.rms < 1.5e-6);
				REQUIRE(lateStats.peak < earlyStats.peak * 0.2);
			}
			else
			{
				REQUIRE(lateStats.peak > 3.0e-5);
				REQUIRE(lateStats.rms > 2.0e-5);
				REQUIRE(lateStats.rms > earlyStats.rms * 0.8);
				REQUIRE(crossings >= 49);
				REQUIRE(crossings <= 51);
				REQUIRE(std::abs(tailFrequency / 10.0 - 1.0) < 0.005);
			}
		}
}

TEST_CASE("Nonlinear TPT ladder 10 Hz quality paths measure ringdown purity and silent startup", "[audio-lab][kobber][ladder-floor-quality][slow]")
{
	using namespace vekt::dsp;
	constexpr double rate = 48'000.0;
	constexpr int blockSize = 128, window = 240'000, length = 6 * window;
	constexpr std::array qualities {
		OversamplingQuality { OversamplingFactor::x2, OversamplingFilter::polyphaseIIR },
		OversamplingQuality { OversamplingFactor::x2, OversamplingFilter::polyphaseFIR },
		OversamplingQuality { OversamplingFactor::x4, OversamplingFilter::polyphaseIIR },
		OversamplingQuality { OversamplingFactor::x4, OversamplingFilter::polyphaseFIR }
	};
	for (const auto quality : qualities)
		for (const auto resonance : { 0.98f, 1.0f })
		{
			OversamplingBank<float> bank(1);
			bank.prepare(blockSize);
			bank.activate(quality);
			vekt::audio_lab::NonlinearTptLadder ladder;
			ladder.prepare(rate * static_cast<double>(bank.getActiveFactor()));
			juce::AudioBuffer<float> buffer(1, blockSize);
			vekt::audio_analysis::SampleStatisticsAccumulator early, late;
			vekt::audio_analysis::SinusoidalProjector fundamental(rate, 10.0, window);
			vekt::audio_analysis::SinusoidalProjector second(rate, 20.0, window);
			vekt::audio_analysis::SinusoidalProjector third(rate, 30.0, window);
			int firstCrossing = -1, lastCrossing = -1, crossings = 0;
			bool finite = true;
			float previous {};
			for (int start = 0; start < length; start += blockSize)
			{
				const auto count = std::min(blockSize, length - start);
				buffer.clear();
				if (start == 0) buffer.setSample(0, 0, 1.0f);
				juce::dsp::AudioBlock<float> full(buffer);
				auto host = full.getSubBlock(0, static_cast<std::size_t>(count));
				const juce::dsp::AudioBlock<const float> input(host);
				auto internal = bank.processSamplesUp(input);
				for (std::size_t index = 0; index < internal.getNumSamples(); ++index)
					internal.setSample(0, static_cast<int>(index), ladder.process(
						internal.getSample(0, static_cast<int>(index)), { 10.0f, resonance, 0.0f }));
				bank.processSamplesDown(host);
				for (int index = 0; index < count; ++index)
				{
					const auto sample = start + index;
					const auto value = buffer.getSample(0, index);
					finite = finite && std::isfinite(value);
					if (sample >= window && sample < 2 * window) early.add(value);
					if (sample >= length - window)
					{
						late.add(value);
						fundamental.add(value);
						second.add(value);
						third.add(value);
						if (previous <= 0.0f && value > 0.0f)
						{
							if (firstCrossing < 0) firstCrossing = sample;
							lastCrossing = sample;
							++crossings;
						}
					}
					previous = value;
				}
			}
			const auto earlyStats = early.result(), lateStats = late.result();
			const auto frequency = crossings > 1
				? (crossings - 1) * rate / (lastCrossing - firstCrossing) : 0.0;
			WARN("development measurement: factor=" << bank.getActiveFactor() << " filter=" << static_cast<int>(quality.filter)
				<< " resonance=" << resonance << " early_peak=" << earlyStats.peak
				<< " late_peak=" << lateStats.peak << " late_rms=" << lateStats.rms
				<< " frequency=" << frequency << " crossings=" << crossings
				<< " fundamental=" << fundamental.peakAmplitude()
				<< " second=" << second.peakAmplitude()
				<< " third=" << third.peakAmplitude());
			REQUIRE(earlyStats.samples == window);
			REQUIRE(finite);
			REQUIRE(lateStats.samples == window);
			REQUIRE(ladder.diagnostics().unconvergedSamples == 0);
			REQUIRE(ladder.diagnostics().nonFiniteSamples == 0);
			REQUIRE(ladder.diagnostics().maximumResidual <= 2.0e-7f);
			REQUIRE(std::isfinite(lateStats.peak));
			REQUIRE(std::isfinite(fundamental.peakAmplitude()));
			REQUIRE(std::isfinite(second.peakAmplitude()));
			REQUIRE(std::isfinite(third.peakAmplitude()));

			// An exactly reset, zero-input deterministic system cannot spontaneously
			// oscillate. This is not a test of physical-noise-seeded startup.
			bank.reset();
			ladder.reset();
			bool silent = true;
			for (int start = 0; start < static_cast<int>(rate); start += blockSize)
			{
				buffer.clear();
				juce::dsp::AudioBlock<float> full(buffer);
				const juce::dsp::AudioBlock<const float> input(full);
				auto internal = bank.processSamplesUp(input);
				for (std::size_t index = 0; index < internal.getNumSamples(); ++index)
					internal.setSample(0, static_cast<int>(index), ladder.process(
						internal.getSample(0, static_cast<int>(index)), { 10.0f, resonance, 0.0f }));
				bank.processSamplesDown(full);
				for (int index = 0; index < blockSize; ++index)
					silent = silent && buffer.getSample(0, index) == 0.0f;
			}
			REQUIRE(silent);
			REQUIRE(ladder.diagnostics().unconvergedSamples == 0);
		}
}

TEST_CASE("Nonlinear TPT ladder noise-seeded startup is measured through quality paths", "[audio-lab][kobber][ladder-startup][slow]")
{
	using namespace vekt::dsp;
	constexpr double rate = 48'000.0;
	constexpr int blockSize = 128, totalSamples = 96'000, tailStart = 72'000;
	constexpr std::array qualities {
		OversamplingQuality { OversamplingFactor::off, OversamplingFilter::polyphaseIIR },
		OversamplingQuality { OversamplingFactor::x2, OversamplingFilter::polyphaseIIR },
		OversamplingQuality { OversamplingFactor::x2, OversamplingFilter::polyphaseFIR },
		OversamplingQuality { OversamplingFactor::x4, OversamplingFilter::polyphaseIIR },
		OversamplingQuality { OversamplingFactor::x4, OversamplingFilter::polyphaseFIR }
	};
	for (const auto quality : qualities)
		for (const auto resonance : { 0.98f, 1.0f })
		{
			OversamplingBank<float> bank(1);
			bank.prepare(blockSize);
			bank.activate(quality);
			vekt::audio_lab::NonlinearTptLadder ladder;
			ladder.prepare(rate * static_cast<double>(bank.getActiveFactor()));
			juce::AudioBuffer<float> buffer(1, blockSize);
			vekt::audio_analysis::SampleStatisticsAccumulator early, late;
			std::uint32_t state = 0x6d6f6e6fu;
			bool finite = true;
			for (int start = 0; start < totalSamples; start += blockSize)
			{
				const auto count = std::min(blockSize, totalSamples - start);
				for (int index = 0; index < count; ++index)
				{
					state = state * 1664525u + 1013904223u;
					const auto noise = static_cast<float>(static_cast<double>(state) / 4294967296.0 - 0.5);
					buffer.setSample(0, index, noise * 2.0e-6f);
				}
				juce::dsp::AudioBlock<float> full(buffer);
				auto host = full.getSubBlock(0, static_cast<std::size_t>(count));
				const juce::dsp::AudioBlock<const float> input(host);
				auto internal = bank.processSamplesUp(input);
				for (std::size_t index = 0; index < internal.getNumSamples(); ++index)
					internal.setSample(0, static_cast<int>(index), ladder.process(
						internal.getSample(0, static_cast<int>(index)), { 1'000.0f, resonance, 0.0f }));
				bank.processSamplesDown(host);
				for (int index = 0; index < count; ++index)
				{
					const auto value = buffer.getSample(0, index);
					finite = finite && std::isfinite(value);
					if (start + index < totalSamples - tailStart) early.add(value);
					if (start + index >= tailStart) late.add(value);
				}
			}
			const auto earlyStats = early.result(), lateStats = late.result();
		WARN("noise startup development measurement: factor=" << bank.getActiveFactor()
			<< " filter=" << static_cast<int>(quality.filter) << " resonance=" << resonance
			<< " early_peak=" << earlyStats.peak << " early_rms=" << earlyStats.rms
			<< " late_peak=" << lateStats.peak << " late_rms=" << lateStats.rms);
		REQUIRE(earlyStats.samples == totalSamples - tailStart);
		REQUIRE(lateStats.samples == totalSamples - tailStart);
		REQUIRE(finite);
		REQUIRE(ladder.diagnostics().unconvergedSamples == 0);
		REQUIRE(ladder.diagnostics().nonFiniteSamples == 0);
		REQUIRE(ladder.diagnostics().maximumResidual <= 2.0e-7f);
		REQUIRE(std::isfinite(lateStats.peak));
		}
}

TEST_CASE("Nonlinear TPT ladder long-form stimuli and modulation have no solver fallback", "[audio-lab][kobber][ladder-stimuli][slow]")
{
	constexpr std::array rates { 44'100.0, 48'000.0, 88'200.0, 96'000.0, 192'000.0 };
	constexpr int sampleCount = 8'192;
	for (const auto rate : rates)
		for (const auto cutoff : { 10.0f, 1'000.0f, static_cast<float>(rate * 0.45) })
			for (const auto resonance : { 0.0f, 0.98f, 1.0f })
				for (const auto drive : { 0.0f, 24.0f })
					for (int stimulus = 0; stimulus < 9; ++stimulus)
					{
						vekt::audio_lab::NonlinearTptLadder first, rerun;
						first.prepare(rate);
						rerun.prepare(rate);
						std::uint32_t noiseState = 0x6d2b79f5u;
						int firstMismatch = -1, firstNonFinite = -1, firstUnbounded = -1;
						for (int sample = 0; sample < sampleCount; ++sample)
						{
							const auto time = static_cast<double>(sample) / rate;
							noiseState ^= noiseState << 13;
							noiseState ^= noiseState >> 17;
							noiseState ^= noiseState << 5;
							const auto noise = static_cast<float>(static_cast<double>(noiseState) / 2'147'483'648.0 - 1.0);
							const auto phase = 2.0 * std::numbers::pi * time;
							const auto input = [&]() -> float
							{
								switch (stimulus)
								{
								case 0: return 0.0f;
								case 1: return 4.0f;
								case 2: return sample == 0 ? 4.0f : 0.0f;
								case 3: return sample == 0 ? -4.0f : 0.0f;
								case 4: return 4.0f * static_cast<float>(std::sin(phase * 173.0));
								case 5: return 4.0f * static_cast<float>(std::sin(phase * (100.0 + 8'000.0 * time)));
								case 6: return 4.0f * noise;
								case 7: return 2.0f * static_cast<float>(std::sin(phase * 300.0)
									+ std::sin(phase * 500.0));
								default: return 2.0f * static_cast<float>(std::sin(phase * 220.0));
								}
							}();
							const auto modulatedCutoff = stimulus == 8 ? std::clamp(cutoff
								* static_cast<float>(1.0 + 0.5 * std::sin(phase * 37.0)), 10.0f,
								static_cast<float>(rate * 0.45)) : cutoff;
							const auto modulatedResonance = stimulus == 8
								? std::clamp(resonance + 0.02f * static_cast<float>(std::sin(phase * 23.0)), 0.0f, 1.0f)
								: resonance;
							const auto modulatedDrive = stimulus == 8
								? drive + 3.0f * static_cast<float>(std::sin(phase * 41.0)) : drive;
							const vekt::audio_lab::NonlinearTptLadderSettings settings {
								modulatedCutoff, modulatedResonance, modulatedDrive };
							const auto a = first.process(input, settings);
							const auto b = rerun.process(input, settings);
							if (!std::isfinite(a) && firstNonFinite < 0) firstNonFinite = sample;
							if (std::abs(a) > 24.0f && firstUnbounded < 0) firstUnbounded = sample;
							if (std::bit_cast<std::uint32_t>(a) != std::bit_cast<std::uint32_t>(b)
								&& firstMismatch < 0) firstMismatch = sample;
						}
						CAPTURE(rate, cutoff, resonance, drive, stimulus,
							first.diagnostics().maximumResidual, first.diagnostics().maximumFeedbackIterations,
							first.diagnostics().maximumStageIterations);
						REQUIRE(firstNonFinite == -1);
						REQUIRE(firstUnbounded == -1);
						REQUIRE(firstMismatch == -1);
						REQUIRE(first.diagnostics().unconvergedSamples == 0);
						REQUIRE(first.diagnostics().nonFiniteSamples == 0);
						REQUIRE(first.diagnostics().maximumResidual <= 2.0e-7f);
						REQUIRE(rerun.diagnostics().unconvergedSamples == 0);
					}
}

TEST_CASE("Nonlinear TPT ladder quality paths converge and preserve block independence", "[audio-lab][kobber][ladder-quality][slow]")
{
	using namespace vekt::dsp;
	constexpr std::array qualities {
		OversamplingQuality { OversamplingFactor::off, OversamplingFilter::polyphaseIIR },
		OversamplingQuality { OversamplingFactor::x2, OversamplingFilter::polyphaseIIR },
		OversamplingQuality { OversamplingFactor::x2, OversamplingFilter::polyphaseFIR },
		OversamplingQuality { OversamplingFactor::x4, OversamplingFilter::polyphaseIIR },
		OversamplingQuality { OversamplingFactor::x4, OversamplingFilter::polyphaseFIR }
	};
	constexpr int sampleCount = 4'096;
	for (const auto rate : { 44'100.0, 48'000.0, 88'200.0, 96'000.0, 192'000.0 })
		for (const auto quality : qualities)
		for (int mode = 0; mode < 4; ++mode)
		{
			const auto render = [&](int blockSize)
			{
				OversamplingBank<float> bank(1);
				bank.prepare(128);
				bank.activate(quality);
				vekt::audio_lab::NonlinearTptLadder ladder;
				ladder.prepare(rate * static_cast<double>(bank.getActiveFactor()));
				juce::AudioBuffer<float> buffer(1, blockSize);
				std::vector<float> output(sampleCount);
				for (int start = 0; start < sampleCount; start += blockSize)
				{
					const auto length = std::min(blockSize, sampleCount - start);
					for (int index = 0; index < length; ++index)
					{
						const auto time = static_cast<double>(start + index) / rate;
						buffer.setSample(0, index, 0.5f * static_cast<float>(
							std::sin(2.0 * std::numbers::pi * 220.0 * time)));
					}
					juce::dsp::AudioBlock<float> fullBlock(buffer);
					auto hostBlock = fullBlock.getSubBlock(0, static_cast<std::size_t>(length));
					const juce::dsp::AudioBlock<const float> inputBlock(hostBlock);
					auto internal = bank.processSamplesUp(inputBlock);
					for (std::size_t index = 0; index < internal.getNumSamples(); ++index)
					{
						const auto time = (static_cast<double>(start)
							+ static_cast<double>(index) / static_cast<double>(bank.getActiveFactor())) / rate;
						const auto cutoff = 1'200.0f + ((mode == 0 || mode == 3) ? 800.0f
							* static_cast<float>(std::sin(2.0 * std::numbers::pi * 37.0 * time)) : 0.0f);
						const auto resonance = 0.85f + ((mode == 1 || mode == 3) ? 0.15f
							* static_cast<float>(std::sin(2.0 * std::numbers::pi * 23.0 * time)) : 0.0f);
						const auto drive = 12.0f + ((mode == 2 || mode == 3) ? 6.0f
							* static_cast<float>(std::sin(2.0 * std::numbers::pi * 41.0 * time)) : 0.0f);
						internal.setSample(0, static_cast<int>(index), ladder.process(
							internal.getSample(0, static_cast<int>(index)), { cutoff, resonance, drive }));
					}
					bank.processSamplesDown(hostBlock);
					for (int index = 0; index < length; ++index)
						output[static_cast<std::size_t>(start + index)] = buffer.getSample(0, index);
				}
				CAPTURE(rate, mode, static_cast<int>(quality.factor), static_cast<int>(quality.filter),
					blockSize, ladder.diagnostics().maximumResidual);
				REQUIRE(ladder.diagnostics().samples == static_cast<std::uint64_t>(sampleCount)
					* bank.getActiveFactor());
				REQUIRE(ladder.diagnostics().unconvergedSamples == 0);
				REQUIRE(ladder.diagnostics().nonFiniteSamples == 0);
				REQUIRE(ladder.diagnostics().maximumResidual <= 2.0e-7f);
				return output;
			};
			const auto baseline = render(31);
			for (const auto blockSize : { 1, 31, 128 })
			{
				const auto comparison = render(blockSize);
				for (int index = 0; index < sampleCount; ++index)
				{
					CAPTURE(rate, mode, static_cast<int>(quality.factor), static_cast<int>(quality.filter),
						blockSize, index);
					REQUIRE(std::isfinite(comparison[static_cast<std::size_t>(index)]));
					REQUIRE(std::bit_cast<std::uint32_t>(baseline[static_cast<std::size_t>(index)])
						== std::bit_cast<std::uint32_t>(comparison[static_cast<std::size_t>(index)]));
				}
			}
		}
}

TEST_CASE("Nonlinear TPT ladder quality paths retain convergence at overload boundaries", "[audio-lab][kobber][ladder-quality]")
{
	using namespace vekt::dsp;
	constexpr std::array qualities {
		OversamplingQuality { OversamplingFactor::off, OversamplingFilter::polyphaseIIR },
		OversamplingQuality { OversamplingFactor::x2, OversamplingFilter::polyphaseIIR },
		OversamplingQuality { OversamplingFactor::x2, OversamplingFilter::polyphaseFIR },
		OversamplingQuality { OversamplingFactor::x4, OversamplingFilter::polyphaseIIR },
		OversamplingQuality { OversamplingFactor::x4, OversamplingFilter::polyphaseFIR }
	};
	for (const auto rate : { 44'100.0, 48'000.0, 88'200.0, 96'000.0, 192'000.0 })
		for (const auto quality : qualities)
			for (const auto cutoffPosition : { 0, 1, 2 })
				for (const auto resonance : { 0.0f, 0.98f, 1.0f })
				{
					OversamplingBank<float> bank(1);
					bank.prepare(31);
					bank.activate(quality);
					const auto internalRate = rate * static_cast<double>(bank.getActiveFactor());
					const auto cutoff = cutoffPosition == 0 ? 10.0f : cutoffPosition == 1
						? 1'000.0f : static_cast<float>(internalRate * 0.45);
					vekt::audio_lab::NonlinearTptLadder ladder;
					ladder.prepare(internalRate);
					juce::AudioBuffer<float> buffer(1, 31);
					int firstNonFinite = -1;
					for (int start = 0; start < 512; start += 31)
					{
						const auto count = std::min(31, 512 - start);
						for (int index = 0; index < count; ++index)
							buffer.setSample(0, index, (start + index) % 2 ? 4.0f : -4.0f);
						juce::dsp::AudioBlock<float> fullBlock(buffer);
						auto hostBlock = fullBlock.getSubBlock(0, static_cast<std::size_t>(count));
						const juce::dsp::AudioBlock<const float> inputBlock(hostBlock);
						auto internal = bank.processSamplesUp(inputBlock);
						for (std::size_t index = 0; index < internal.getNumSamples(); ++index)
							internal.setSample(0, static_cast<int>(index), ladder.process(
								internal.getSample(0, static_cast<int>(index)), { cutoff, resonance, 24.0f }));
						bank.processSamplesDown(hostBlock);
						for (int index = 0; index < count; ++index)
							if (!std::isfinite(buffer.getSample(0, index)) && firstNonFinite < 0)
								firstNonFinite = start + index;
					}
					CAPTURE(rate, static_cast<int>(quality.factor), static_cast<int>(quality.filter),
						cutoff, resonance, firstNonFinite, ladder.diagnostics().maximumResidual);
					REQUIRE(firstNonFinite == -1);
					REQUIRE(ladder.diagnostics().samples == 512 * bank.getActiveFactor());
					REQUIRE(ladder.diagnostics().unconvergedSamples == 0);
					REQUIRE(ladder.diagnostics().nonFiniteSamples == 0);
					REQUIRE(ladder.diagnostics().maximumResidual <= 2.0e-7f);
				}
}

TEST_CASE("Nonlinear TPT ladder 8x and 16x FIR paths remain block-independent under modulation and overload", "[audio-lab][kobber][ladder-high-quality-stability]")
{
	using namespace vekt::dsp;
	constexpr int sampleCount = 512;
	for (const auto rate : { 44'100.0, 48'000.0, 88'200.0, 96'000.0, 192'000.0 })
		for (const auto factor : { OversamplingFactor::x8, OversamplingFactor::x16 })
			for (const auto stimulus : { 0, 1, 2 })
			{
				const auto render = [&](int blockSize)
				{
					OversamplingBank<float> bank(1);
					bank.prepare(127);
					bank.activate({ factor, OversamplingFilter::polyphaseFIR });
					vekt::audio_lab::NonlinearTptLadder ladder;
					ladder.prepare(rate * static_cast<double>(bank.getActiveFactor()));
					juce::AudioBuffer<float> buffer(1, blockSize);
					std::vector<float> output(sampleCount);
					float maximumInternalInput {};
					for (int start = 0; start < sampleCount; start += blockSize)
					{
						const auto count = std::min(blockSize, sampleCount - start);
						for (int index = 0; index < count; ++index)
							buffer.setSample(0, index, stimulus == 0
								? 0.5f * static_cast<float>(std::sin(2.0 * std::numbers::pi
									* 7'000.0 * (start + index) / rate))
								: (stimulus == 1 ? ((start + index) % 2 == 0 ? 4.0f : -4.0f)
									: ((start + index) / 64 % 2 == 0 ? 4.0f : -4.0f)));
						juce::dsp::AudioBlock<float> full(buffer);
						auto host = full.getSubBlock(0, static_cast<std::size_t>(count));
						const juce::dsp::AudioBlock<const float> input(host);
						auto internal = bank.processSamplesUp(input);
						for (std::size_t index = 0; index < internal.getNumSamples(); ++index)
						{
							const auto internalInput = internal.getSample(0, static_cast<int>(index));
							maximumInternalInput = std::max(maximumInternalInput, std::abs(internalInput));
							const auto time = (static_cast<double>(start)
								+ static_cast<double>(index) / static_cast<double>(bank.getActiveFactor())) / rate;
							const auto cutoff = stimulus == 0 ? 1'200.0f + 800.0f
								* static_cast<float>(std::sin(2.0 * std::numbers::pi * 37.0 * time))
								: static_cast<float>(rate * static_cast<double>(bank.getActiveFactor()) * 0.45);
							const auto resonance = stimulus == 0 ? 0.85f + 0.15f
								* static_cast<float>(std::sin(2.0 * std::numbers::pi * 23.0 * time)) : 1.0f;
							const auto drive = stimulus == 0 ? 12.0f + 6.0f
								* static_cast<float>(std::sin(2.0 * std::numbers::pi * 41.0 * time)) : 24.0f;
							internal.setSample(0, static_cast<int>(index), ladder.process(
								internalInput, { cutoff, resonance, drive }));
						}
						bank.processSamplesDown(host);
						for (int index = 0; index < count; ++index)
							output[static_cast<std::size_t>(start + index)] = buffer.getSample(0, index);
					}
					CAPTURE(rate, static_cast<int>(factor), stimulus, blockSize,
						ladder.diagnostics().maximumResidual, maximumInternalInput);
					if (stimulus == 2) REQUIRE(maximumInternalInput > 3.0f);
					REQUIRE(ladder.diagnostics().samples == static_cast<std::uint64_t>(sampleCount)
						* bank.getActiveFactor());
					REQUIRE(ladder.diagnostics().unconvergedSamples == 0);
					REQUIRE(ladder.diagnostics().nonFiniteSamples == 0);
					REQUIRE(ladder.diagnostics().maximumResidual <= 2.0e-7f);
					return output;
				};
				const auto baseline = render(127);
				const auto singleSample = render(1);
				for (int sample = 0; sample < sampleCount; ++sample)
				{
					CAPTURE(rate, static_cast<int>(factor), stimulus, sample);
					REQUIRE(std::isfinite(baseline[static_cast<std::size_t>(sample)]));
					REQUIRE(std::bit_cast<std::uint32_t>(baseline[static_cast<std::size_t>(sample)])
						== std::bit_cast<std::uint32_t>(singleSample[static_cast<std::size_t>(sample)]));
				}
			}
}

TEST_CASE("Nonlinear TPT ladder quality paths measure a coherent fifth-harmonic alias", "[audio-lab][kobber][ladder-alias]")
{
	using namespace vekt::dsp;
	constexpr double rate = 48'000.0;
	constexpr double fundamentalHz = 7'000.0;
	constexpr double foldedFifthHz = 13'000.0; // 5 * 7 kHz folds at 48 kHz.
	constexpr int settleSamples = 12'000;
	constexpr int measureSamples = 12'000;
	constexpr std::array qualities {
		OversamplingQuality { OversamplingFactor::off, OversamplingFilter::polyphaseIIR },
		OversamplingQuality { OversamplingFactor::x2, OversamplingFilter::polyphaseIIR },
		OversamplingQuality { OversamplingFactor::x2, OversamplingFilter::polyphaseFIR },
		OversamplingQuality { OversamplingFactor::x4, OversamplingFilter::polyphaseIIR },
		OversamplingQuality { OversamplingFactor::x4, OversamplingFilter::polyphaseFIR }
	};
	std::array<double, qualities.size()> aliasDbc {};
	for (std::size_t path = 0; path < qualities.size(); ++path)
	{
		OversamplingBank<float> bank(1);
		bank.prepare(64);
		bank.activate(qualities[path]);
		vekt::audio_lab::NonlinearTptLadder ladder;
		ladder.prepare(rate * static_cast<double>(bank.getActiveFactor()));
		juce::AudioBuffer<float> buffer(1, 64);
		vekt::audio_analysis::SinusoidalProjector fundamental(rate, fundamentalHz, settleSamples);
		vekt::audio_analysis::SinusoidalProjector alias(rate, foldedFifthHz, settleSamples);
		for (int start = 0; start < settleSamples + measureSamples; start += 64)
		{
			const auto count = std::min(64, settleSamples + measureSamples - start);
			for (int index = 0; index < count; ++index)
				buffer.setSample(0, index, 0.5f * static_cast<float>(std::sin(
					2.0 * std::numbers::pi * fundamentalHz * (start + index) / rate)));
			juce::dsp::AudioBlock<float> fullBlock(buffer);
			auto hostBlock = fullBlock.getSubBlock(0, static_cast<std::size_t>(count));
			const juce::dsp::AudioBlock<const float> inputBlock(hostBlock);
			auto internal = bank.processSamplesUp(inputBlock);
			for (std::size_t index = 0; index < internal.getNumSamples(); ++index)
				internal.setSample(0, static_cast<int>(index), ladder.process(
					internal.getSample(0, static_cast<int>(index)), { 10'000.0f, 0.5f, 12.0f }));
			bank.processSamplesDown(hostBlock);
			for (int index = 0; index < count; ++index)
				if (start + index >= settleSamples)
				{
					fundamental.add(buffer.getSample(0, index));
					alias.add(buffer.getSample(0, index));
				}
		}
		const auto fundamentalPeak = fundamental.peakAmplitude();
		const auto aliasPeak = alias.peakAmplitude();
		CAPTURE(path, fundamentalPeak, aliasPeak, ladder.diagnostics().maximumResidual);
		REQUIRE(std::isfinite(fundamentalPeak));
		REQUIRE(std::isfinite(aliasPeak));
		REQUIRE(fundamentalPeak > 1.0e-5);
		REQUIRE(ladder.diagnostics().unconvergedSamples == 0);
		REQUIRE(ladder.diagnostics().nonFiniteSamples == 0);
		aliasDbc[path] = 20.0 * std::log10(std::max(aliasPeak, 1.0e-15) / fundamentalPeak);
	}
	INFO("folded fifth in dBc: off=" << aliasDbc[0] << ", 2x IIR=" << aliasDbc[1]
		<< ", 2x FIR=" << aliasDbc[2] << ", 4x IIR=" << aliasDbc[3]
		<< ", 4x FIR=" << aliasDbc[4]);
	for (std::size_t path = 1; path < aliasDbc.size(); ++path)
		REQUIRE(aliasDbc[path] < aliasDbc[0]);
}

TEST_CASE("Nonlinear TPT ladder folded fifth is measured across host rates and drive", "[audio-lab][kobber][ladder-alias-matrix][slow]")
{
	using namespace vekt::dsp;
	constexpr std::array qualities {
		OversamplingQuality { OversamplingFactor::off, OversamplingFilter::polyphaseIIR },
		OversamplingQuality { OversamplingFactor::x2, OversamplingFilter::polyphaseIIR },
		OversamplingQuality { OversamplingFactor::x2, OversamplingFilter::polyphaseFIR },
		OversamplingQuality { OversamplingFactor::x4, OversamplingFilter::polyphaseIIR },
		OversamplingQuality { OversamplingFactor::x4, OversamplingFilter::polyphaseFIR }
	};
	// The 4,800-sample window contains exactly 700 fundamental cycles and 1,300
	// folded-fifth cycles at every rate. Distinct harmonic bins do not leak here.
	constexpr int window = 4'800, blockSize = 64;
	for (const auto rate : { 44'100.0, 48'000.0, 88'200.0, 96'000.0, 192'000.0 })
		for (const auto drive : { 12.0f, 24.0f })
			for (const auto resonance : { 0.5f, 0.98f })
			{
				std::array<double, qualities.size()> aliasDbc {};
				double worstSeventhDbc = -300.0, worstNinthDbc = -300.0;
				std::size_t worstSeventhPath = 0, worstNinthPath = 0;
				for (std::size_t path = 0; path < qualities.size(); ++path)
				{
					OversamplingBank<float> bank(1);
					bank.prepare(blockSize);
					bank.activate(qualities[path]);
					vekt::audio_lab::NonlinearTptLadder ladder;
					ladder.prepare(rate * static_cast<double>(bank.getActiveFactor()));
					juce::AudioBuffer<float> buffer(1, blockSize);
					vekt::audio_analysis::SinusoidalProjector fundamental(rate, rate * 7.0 / 48.0, window);
					vekt::audio_analysis::SinusoidalProjector foldedFifth(rate, rate * 13.0 / 48.0, window);
					// 7 * 7/48 folds to 1/48; 9 * 7/48 folds to 15/48.
					vekt::audio_analysis::SinusoidalProjector foldedSeventh(rate, rate / 48.0, window);
					vekt::audio_analysis::SinusoidalProjector foldedNinth(rate, rate * 15.0 / 48.0, window);
					for (int start = 0; start < 2 * window; start += blockSize)
					{
						const auto count = std::min(blockSize, 2 * window - start);
						for (int index = 0; index < count; ++index)
							buffer.setSample(0, index, 0.5f * static_cast<float>(std::sin(
								2.0 * std::numbers::pi * 7.0 * (start + index) / 48.0)));
						juce::dsp::AudioBlock<float> fullBlock(buffer);
						auto hostBlock = fullBlock.getSubBlock(0, static_cast<std::size_t>(count));
						const juce::dsp::AudioBlock<const float> inputBlock(hostBlock);
						auto internal = bank.processSamplesUp(inputBlock);
						for (std::size_t index = 0; index < internal.getNumSamples(); ++index)
							internal.setSample(0, static_cast<int>(index), ladder.process(
								internal.getSample(0, static_cast<int>(index)),
								{ static_cast<float>(rate * 10.0 / 48.0), resonance, drive }));
						bank.processSamplesDown(hostBlock);
						for (int index = 0; index < count; ++index)
							if (start + index >= window)
							{
								fundamental.add(buffer.getSample(0, index));
								foldedFifth.add(buffer.getSample(0, index));
								foldedSeventh.add(buffer.getSample(0, index));
								foldedNinth.add(buffer.getSample(0, index));
							}
					}
					const auto fundamentalPeak = fundamental.peakAmplitude();
					const auto foldedPeak = foldedFifth.peakAmplitude();
					CAPTURE(rate, drive, resonance, path, fundamentalPeak, foldedPeak,
						ladder.diagnostics().maximumResidual);
					REQUIRE(fundamentalPeak > 1.0e-5);
					REQUIRE(std::isfinite(foldedPeak));
					REQUIRE(ladder.diagnostics().unconvergedSamples == 0);
					REQUIRE(ladder.diagnostics().nonFiniteSamples == 0);
					aliasDbc[path] = 20.0 * std::log10(std::max(foldedPeak, 1.0e-15) / fundamentalPeak);
					const auto seventhDbc = 20.0 * std::log10(
						std::max(foldedSeventh.peakAmplitude(), 1.0e-15) / fundamentalPeak);
					const auto ninthDbc = 20.0 * std::log10(
						std::max(foldedNinth.peakAmplitude(), 1.0e-15) / fundamentalPeak);
					CAPTURE(rate, drive, resonance, path, seventhDbc, ninthDbc);
					REQUIRE(std::isfinite(seventhDbc));
					REQUIRE(std::isfinite(ninthDbc));
					if (path != 0)
					{
						if (seventhDbc > worstSeventhDbc)
						{
							worstSeventhDbc = seventhDbc;
							worstSeventhPath = path;
						}
						if (ninthDbc > worstNinthDbc)
						{
							worstNinthDbc = ninthDbc;
							worstNinthPath = path;
						}
					}
				}
				WARN("development alias audit: rate=" << rate << " drive=" << drive
				<< " resonance=" << resonance << " worst_oversampled_seventh_dBc="
				<< worstSeventhDbc << " seventh_path=" << worstSeventhPath
				<< " worst_oversampled_ninth_dBc=" << worstNinthDbc
				<< " ninth_path=" << worstNinthPath);
				for (std::size_t path = 1; path < aliasDbc.size(); ++path)
				{
					CAPTURE(rate, drive, resonance, path, aliasDbc[path], aliasDbc[0]);
					REQUIRE(aliasDbc[path] < aliasDbc[0]);
					// This tests only the coherent folded fifth, not the maximum
					// alias spur over the spectrum or an accepted product limit.
					REQUIRE(aliasDbc[path] < (qualities[path].factor == OversamplingFactor::x4
						? -80.0 : -60.0));
				}
			}
}

TEST_CASE("Nonlinear TPT ladder coherent single-tone host-band bins are audited", "[audio-lab][kobber][ladder-alias-spectrum][slow]")
{
	using namespace vekt::dsp;
	constexpr int window = 4'800, blockSize = 96;
	constexpr std::array qualities {
		OversamplingQuality { OversamplingFactor::off, OversamplingFilter::polyphaseIIR },
		OversamplingQuality { OversamplingFactor::x2, OversamplingFilter::polyphaseIIR },
		OversamplingQuality { OversamplingFactor::x2, OversamplingFilter::polyphaseFIR },
		OversamplingQuality { OversamplingFactor::x4, OversamplingFilter::polyphaseIIR },
		OversamplingQuality { OversamplingFactor::x4, OversamplingFilter::polyphaseFIR },
		OversamplingQuality { OversamplingFactor::x8, OversamplingFilter::polyphaseFIR },
		OversamplingQuality { OversamplingFactor::x16, OversamplingFilter::polyphaseFIR }
	};
	// Fundamental bin 7 on a 48-sample coherent period; bins 14 and 21
	// are intentional in-band harmonics, not aliases.
	for (const auto rate : { 44'100.0, 48'000.0, 88'200.0, 96'000.0, 192'000.0 })
		for (const auto drive : { 12.0f, 24.0f })
		for (const auto resonance : { 0.5f, 0.98f })
			for (std::size_t path = 0; path < qualities.size(); ++path)
			{
				OversamplingBank<float> bank(1);
				bank.prepare(blockSize);
				bank.activate(qualities[path]);
				vekt::audio_lab::NonlinearTptLadder ladder;
				ladder.prepare(rate * static_cast<double>(bank.getActiveFactor()));
				juce::AudioBuffer<float> buffer(1, blockSize);
				std::vector<vekt::audio_analysis::SinusoidalProjector> bins;
				for (int bin = 1; bin < 24; ++bin)
					bins.emplace_back(rate, rate * bin / 48.0, window);
				bool finite = true;
				for (int start = 0; start < 2 * window; start += blockSize)
				{
					for (int index = 0; index < blockSize; ++index)
						buffer.setSample(0, index, 0.5f * static_cast<float>(std::sin(
							2.0 * std::numbers::pi * 7.0 * (start + index) / 48.0)));
					juce::dsp::AudioBlock<float> host(buffer);
					const juce::dsp::AudioBlock<const float> input(host);
					auto internal = bank.processSamplesUp(input);
					for (std::size_t index = 0; index < internal.getNumSamples(); ++index)
						internal.setSample(0, static_cast<int>(index), ladder.process(
							internal.getSample(0, static_cast<int>(index)),
							{ static_cast<float>(rate * 10.0 / 48.0), resonance, drive }));
					bank.processSamplesDown(host);
					if (start >= window)
						for (int index = 0; index < blockSize; ++index)
						{
							const auto value = buffer.getSample(0, index);
							finite = finite && std::isfinite(value);
							for (auto& bin : bins) bin.add(value);
						}
				}
				const auto fundamental = bins[6].peakAmplitude();
				double largestSpur {};
				int largestBin = 0;
				for (int bin = 1; bin < 24; ++bin)
				{
					if (bin == 7 || bin == 14 || bin == 21) continue;
					const auto peak = bins[static_cast<std::size_t>(bin - 1)].peakAmplitude();
					if (peak > largestSpur) { largestSpur = peak; largestBin = bin; }
				}
				const auto spurDbc = 20.0 * std::log10(std::max(largestSpur, 1.0e-15) / fundamental);
			WARN("single-tone coherent spectrum: rate=" << rate << " drive=" << drive << " resonance="
				<< resonance << " factor=" << bank.getActiveFactor()
				<< " filter=" << static_cast<int>(qualities[path].filter)
				<< " largest_bin=" << largestBin
				<< " largest_spur_dBc=" << spurDbc);
			CAPTURE(rate, drive, resonance, path, fundamental, largestBin, spurDbc);
			REQUIRE(finite);
			REQUIRE(fundamental > 1.0e-5);
			REQUIRE(std::isfinite(spurDbc));
			// Measured bounds for this normalized coherent sine only, not a
			// product alias specification or a full-band modulation limit.
			if (qualities[path].factor == OversamplingFactor::x8)
				REQUIRE(spurDbc < (drive == 12.0f ? -110.0 : -75.0));
			if (qualities[path].factor == OversamplingFactor::x16)
				REQUIRE(spurDbc < (drive == 12.0f ? -110.0 : -100.0));
			REQUIRE(ladder.diagnostics().unconvergedSamples == 0);
			REQUIRE(ladder.diagnostics().nonFiniteSamples == 0);
			}
}

TEST_CASE("Nonlinear TPT ladder internal spur precedes downsampling", "[audio-lab][kobber][ladder-internal-alias]")
{
	using namespace vekt::dsp;
	constexpr double rate = 48'000.0;
	constexpr int window = 4'800, blockSize = 96;
	for (const auto factor : { OversamplingFactor::x2, OversamplingFactor::x4 })
	{
		OversamplingBank<float> bank(1);
		bank.prepare(blockSize);
		bank.activate({ factor, OversamplingFilter::polyphaseFIR });
		const auto internalRate = rate * static_cast<double>(bank.getActiveFactor());
		const auto spurHz = factor == OversamplingFactor::x2 ? 5'000.0 : 3'000.0;
		const auto internalWindow = window * bank.getActiveFactor();
		vekt::audio_analysis::SinusoidalProjector inputSpur(internalRate, spurHz, internalWindow);
		vekt::audio_analysis::SinusoidalProjector outputSpur(internalRate, spurHz, internalWindow);
		vekt::audio_analysis::SinusoidalProjector internalFundamental(internalRate, 7'000.0, internalWindow);
		vekt::audio_analysis::SinusoidalProjector hostSpur(rate, spurHz, window);
		vekt::audio_analysis::SinusoidalProjector hostFundamental(rate, 7'000.0, window);
		vekt::audio_lab::NonlinearTptLadder ladder;
		ladder.prepare(internalRate);
		juce::AudioBuffer<float> buffer(1, blockSize);
		for (int start = 0; start < 2 * window; start += blockSize)
		{
			for (int index = 0; index < blockSize; ++index)
				buffer.setSample(0, index, 0.5f * static_cast<float>(std::sin(
					2.0 * std::numbers::pi * 7.0 * (start + index) / 48.0)));
			juce::dsp::AudioBlock<float> host(buffer);
			const juce::dsp::AudioBlock<const float> input(host);
			auto internal = bank.processSamplesUp(input);
			for (std::size_t index = 0; index < internal.getNumSamples(); ++index)
			{
				const auto before = internal.getSample(0, static_cast<int>(index));
				const auto after = ladder.process(before, { 10'000.0f, 0.98f, 12.0f });
				internal.setSample(0, static_cast<int>(index), after);
				if (start >= window)
				{
					inputSpur.add(before);
					outputSpur.add(after);
					internalFundamental.add(after);
				}
			}
			bank.processSamplesDown(host);
			if (start >= window)
				for (int index = 0; index < blockSize; ++index)
				{
					hostSpur.add(buffer.getSample(0, index));
					hostFundamental.add(buffer.getSample(0, index));
				}
		}
		const auto internalDbc = 20.0 * std::log10(outputSpur.peakAmplitude()
			/ internalFundamental.peakAmplitude());
		const auto hostDbc = 20.0 * std::log10(hostSpur.peakAmplitude()
			/ hostFundamental.peakAmplitude());
		WARN("internal fold audit: factor=" << bank.getActiveFactor()
			<< " spur_hz=" << spurHz << " upsampled_input_spur=" << inputSpur.peakAmplitude()
			<< " pre_downsample_dBc=" << internalDbc << " host_dBc=" << hostDbc);
		CAPTURE(spurHz, inputSpur.peakAmplitude(), internalDbc, hostDbc);
		REQUIRE(inputSpur.peakAmplitude() < 1.0e-6);
		REQUIRE(std::isfinite(internalDbc));
		REQUIRE(std::isfinite(hostDbc));
		REQUIRE(std::abs(internalDbc - hostDbc) < 0.1);
		REQUIRE(ladder.diagnostics().unconvergedSamples == 0);
		REQUIRE(ladder.diagnostics().nonFiniteSamples == 0);
	}
}

TEST_CASE("Nonlinear TPT ladder 2x spur is compared with the 16x 13th harmonic", "[audio-lab][kobber][ladder-internal-alias]")
{
	using namespace vekt::dsp;
	constexpr double rate = 48'000.0;
	constexpr int window = 4'800, blockSize = 96;
	std::array<double, 2> harmonicDbc {};
	for (std::size_t path = 0; path < harmonicDbc.size(); ++path)
	{
		const auto factor = path == 0 ? OversamplingFactor::x2 : OversamplingFactor::x16;
		OversamplingBank<float> bank(1);
		bank.prepare(blockSize);
		bank.activate({ factor, OversamplingFilter::polyphaseFIR });
		const auto internalRate = rate * static_cast<double>(bank.getActiveFactor());
		const auto measuredHz = path == 0 ? 5'000.0 : 91'000.0;
		vekt::audio_analysis::SinusoidalProjector harmonic(internalRate, measuredHz,
			window * bank.getActiveFactor());
		vekt::audio_analysis::SinusoidalProjector fundamental(internalRate, 7'000.0,
			window * bank.getActiveFactor());
		vekt::audio_lab::NonlinearTptLadder ladder;
		ladder.prepare(internalRate);
		juce::AudioBuffer<float> buffer(1, blockSize);
		for (int start = 0; start < 2 * window; start += blockSize)
		{
			for (int index = 0; index < blockSize; ++index)
				buffer.setSample(0, index, 0.5f * static_cast<float>(std::sin(
					2.0 * std::numbers::pi * 7.0 * (start + index) / 48.0)));
			juce::dsp::AudioBlock<float> host(buffer);
			const juce::dsp::AudioBlock<const float> input(host);
			auto internal = bank.processSamplesUp(input);
			for (std::size_t index = 0; index < internal.getNumSamples(); ++index)
			{
				const auto output = ladder.process(internal.getSample(0, static_cast<int>(index)),
					{ 10'000.0f, 0.98f, 12.0f });
				if (start >= window)
				{
					harmonic.add(output);
					fundamental.add(output);
				}
			}
		}
		harmonicDbc[path] = 20.0 * std::log10(harmonic.peakAmplitude() / fundamental.peakAmplitude());
		WARN("internal 13th audit: factor=" << bank.getActiveFactor()
			<< " frequency_hz=" << measuredHz << " dBc=" << harmonicDbc[path]);
		REQUIRE(fundamental.peakAmplitude() > 1.0e-5);
		REQUIRE(std::isfinite(harmonicDbc[path]));
		REQUIRE(ladder.diagnostics().unconvergedSamples == 0);
		REQUIRE(ladder.diagnostics().nonFiniteSamples == 0);
	}
	// 91 kHz is above the 96 kHz path's Nyquist frequency, but below
	// the 768 kHz path's Nyquist frequency. This is a diagnostic, not an
	// assertion that the two discretizations generate identical harmonics.
	REQUIRE(harmonicDbc[0] > -60.0);
	REQUIRE(harmonicDbc[1] < -100.0);
}

TEST_CASE("Nonlinear TPT ladder 2x spur is compared with a substepped same-input reference", "[audio-lab][kobber][ladder-internal-alias][slow]")
{
	using namespace vekt::dsp;
	constexpr double internalRate = 96'000.0;
	constexpr int window = 4'800, blockSize = 96;
	constexpr std::array substeps { 1, 4, 16, 32 };
	for (const auto filter : { OversamplingFilter::polyphaseIIR, OversamplingFilter::polyphaseFIR })
	{
		OversamplingBank<float> bank(1);
		bank.prepare(blockSize);
		bank.activate({ OversamplingFactor::x2, filter });
		vekt::audio_lab::NonlinearTptLadder candidate;
		candidate.prepare(internalRate);
		std::array<vekt::audio_lab::NonlinearTptLadderOfflineReference, substeps.size()> references;
		for (std::size_t path = 0; path < references.size(); ++path)
			references[path].prepare(internalRate, substeps[path]);
		std::vector<vekt::audio_analysis::SinusoidalProjector> fundamental, spur;
		fundamental.reserve(substeps.size() + 1);
		spur.reserve(substeps.size() + 1);
		for (std::size_t path = 0; path <= substeps.size(); ++path)
		{
			fundamental.emplace_back(internalRate, 7'000.0, window * 2);
			spur.emplace_back(internalRate, 5'000.0, window * 2);
		}
		juce::AudioBuffer<float> buffer(1, blockSize);
		for (int start = 0; start < 2 * window; start += blockSize)
		{
			for (int index = 0; index < blockSize; ++index)
				buffer.setSample(0, index, 0.5f * static_cast<float>(std::sin(
					2.0 * std::numbers::pi * 7.0 * (start + index) / 48.0)));
			juce::dsp::AudioBlock<float> host(buffer);
			const juce::dsp::AudioBlock<const float> input(host);
			const auto internal = bank.processSamplesUp(input);
			for (std::size_t index = 0; index < internal.getNumSamples(); ++index)
			{
				const auto sample = internal.getSample(0, static_cast<int>(index));
				const auto output = candidate.process(sample, { 10'000.0f, 0.98f, 12.0f });
				if (start >= window)
				{
					fundamental[0].add(output);
					spur[0].add(output);
				}
				for (std::size_t path = 0; path < references.size(); ++path)
				{
					const auto referenceOutput = references[path].process(sample,
						{ 10'000.0, 0.98, 12.0 });
					if (start >= window)
					{
						fundamental[path + 1].add(referenceOutput);
						spur[path + 1].add(referenceOutput);
					}
				}
			}
		}
		std::array<double, substeps.size() + 1> spurDbc {};
		for (std::size_t path = 0; path < spurDbc.size(); ++path)
		{
			REQUIRE(fundamental[path].peakAmplitude() > 1.0e-5);
			spurDbc[path] = 20.0 * std::log10(std::max(spur[path].peakAmplitude(), 1.0e-15)
				/ fundamental[path].peakAmplitude());
			WARN("same-upsampled-input internal 5 kHz: filter="
				<< (filter == OversamplingFilter::polyphaseIIR ? "IIR" : "FIR")
				<< " substeps=" << (path == 0 ? 0 : substeps[path - 1])
				<< " dBc=" << spurDbc[path] << " peak=" << spur[path].peakAmplitude());
			REQUIRE(std::isfinite(spurDbc[path]));
		}
		REQUIRE(candidate.diagnostics().unconvergedSamples == 0);
		REQUIRE(candidate.diagnostics().nonFiniteSamples == 0);
		for (const auto& reference : references)
		{
			REQUIRE(reference.diagnostics().unconvergedSteps == 0);
			REQUIRE(reference.diagnostics().nonFiniteSteps == 0);
			REQUIRE(reference.diagnostics().maximumResidual <= 1.0e-13);
		}
		// The one-step double reference solves the same TPT equations at the
		// same internal rate. Extra steps test convergence with this exact input.
		REQUIRE(std::abs(spurDbc[0] - spurDbc[1]) < 0.1);
		REQUIRE(spurDbc[2] < spurDbc[1] - 25.0);
		REQUIRE(std::abs(spurDbc[4] - spurDbc[3]) < 0.1);
	}
}

TEST_CASE("One-times offline reference filters internal samples before decimation", "[audio-lab][kobber][ladder-bandlimited-reference][slow]")
{
	constexpr double rate = 48'000.0;
	constexpr int window = 4'800;
	constexpr double inputHz = 7'000.0;
	const auto coefficients = [](int factor)
	{
		const auto radius = factor * 32;
		std::vector<double> taps(static_cast<std::size_t>(2 * radius + 1));
		const auto internalRate = rate * factor;
		const auto cutoff = rate * 0.42;
		double total {};
		for (int offset = -radius; offset <= radius; ++offset)
		{
			const auto phase = 2.0 * std::numbers::pi * cutoff * offset / internalRate;
			const auto sinc = offset == 0 ? 1.0 : std::sin(phase) / phase;
			const auto fraction = static_cast<double>(offset + radius) / (2 * radius);
			const auto blackman = 0.42 - 0.5 * std::cos(2.0 * std::numbers::pi * fraction)
				+ 0.08 * std::cos(4.0 * std::numbers::pi * fraction);
			const auto tap = (2.0 * cutoff / internalRate) * sinc * blackman;
			taps[static_cast<std::size_t>(offset + radius)] = tap;
			total += tap;
		}
		for (auto& tap : taps) tap /= total;
		return taps;
	};
	const auto render = [&](int factor, float drive)
	{
		const auto taps = coefficients(factor);
		const auto radius = factor * 32;
		// Symmetric offline FIR: +/-32 host samples of context, not a
		// causal production filter. The last window has future context.
		const auto hostSamples = 2 * window + 33;
		std::vector<double> internal(static_cast<std::size_t>(hostSamples * factor));
		vekt::audio_lab::NonlinearTptLadderOfflineReference reference;
		reference.prepare(rate, factor);
		for (int sample = 0; sample < hostSamples; ++sample)
		{
			const auto input = 0.5f * static_cast<float>(std::sin(
				2.0 * std::numbers::pi * inputHz * sample / rate));
			const auto begin = static_cast<std::size_t>(sample * factor);
			const auto endSample = reference.process(input, { 10'000.0, 0.98, drive },
				std::span<double>(internal).subspan(begin, static_cast<std::size_t>(factor)));
			REQUIRE(std::bit_cast<std::uint64_t>(endSample)
				== std::bit_cast<std::uint64_t>(internal[begin + static_cast<std::size_t>(factor - 1)]));
		}
		REQUIRE(reference.diagnostics().unconvergedSteps == 0);
		REQUIRE(reference.diagnostics().nonFiniteSteps == 0);
		std::vector<double> output(static_cast<std::size_t>(window));
		for (int sample = window; sample < 2 * window; ++sample)
		{
			const auto center = sample * factor + factor - 1;
			double value {};
			for (int offset = -radius; offset <= radius; ++offset)
				value += taps[static_cast<std::size_t>(offset + radius)]
					* internal[static_cast<std::size_t>(center + offset)];
			output[static_cast<std::size_t>(sample - window)] = value;
		}
		return output;
	};
	for (const auto factor : { 16, 32 })
	{
		const auto taps = coefficients(factor);
		const auto radius = factor * 32;
		const auto gainAt = [&](double frequency)
		{
			double real {}, imaginary {};
			for (int offset = -radius; offset <= radius; ++offset)
			{
				const auto phase = 2.0 * std::numbers::pi * frequency * offset / (rate * factor);
				real += taps[static_cast<std::size_t>(offset + radius)] * std::cos(phase);
				imaginary += taps[static_cast<std::size_t>(offset + radius)] * std::sin(phase);
			}
			return std::hypot(real, imaginary);
		};
		const auto passbandGain = gainAt(inputHz);
		const auto stopbandGain = gainAt(47'000.0);
		const auto otherStopbandGain = gainAt(43'000.0);
		double largestPassbandError {}, largestStopbandGain {};
		for (int frequency = 1'000; frequency <= 15'000; frequency += 1'000)
			largestPassbandError = std::max(largestPassbandError,
				std::abs(gainAt(static_cast<double>(frequency)) - 1.0));
		for (int frequency = 25'000; frequency <= 47'000; frequency += 1'000)
			largestStopbandGain = std::max(largestStopbandGain,
				gainAt(static_cast<double>(frequency)));
		CAPTURE(factor, passbandGain, stopbandGain, otherStopbandGain,
			largestPassbandError, largestStopbandGain);
		REQUIRE(std::abs(gainAt(inputHz) - 1.0) < 1.0e-4);
		REQUIRE(gainAt(47'000.0) < 1.0e-4);
		REQUIRE(gainAt(43'000.0) < 1.0e-4);
		REQUIRE(largestPassbandError < 1.0e-4);
		REQUIRE(largestStopbandGain < 1.0e-4);
		WARN("1x offline FIR: factor=" << factor << " max_1to15k_gain_error="
			<< largestPassbandError << " max_25to47k_gain=" << largestStopbandGain);
		// 47 kHz would fold to 1 kHz if decimated without filtering.
		vekt::audio_analysis::SinusoidalProjector folded(rate, 1'000.0, window);
		for (int sample = window; sample < 2 * window; ++sample)
		{
			double filtered {};
			for (int offset = -radius; offset <= radius; ++offset)
			{
				const auto time = (sample * factor + factor - 1 + offset) / (rate * factor);
				filtered += taps[static_cast<std::size_t>(offset + radius)]
					* std::sin(2.0 * std::numbers::pi * 47'000.0 * time);
			}
			folded.add(filtered);
		}
		REQUIRE(folded.peakAmplitude() < 1.0e-4);
	}
	for (const auto drive : { 12.0f, 24.0f })
	{
		const auto reference16 = render(16, drive);
		const auto reference32 = render(32, drive);
		vekt::audio_lab::NonlinearTptLadder candidate;
		candidate.prepare(rate);
		std::vector<double> host(static_cast<std::size_t>(window));
		for (int sample = 0; sample < 2 * window; ++sample)
		{
			const auto input = 0.5f * static_cast<float>(std::sin(
				2.0 * std::numbers::pi * inputHz * sample / rate));
			const auto value = candidate.process(input, { 10'000.0f, 0.98f, drive });
			if (sample >= window) host[static_cast<std::size_t>(sample - window)] = value;
		}
		REQUIRE(candidate.diagnostics().unconvergedSamples == 0);
		REQUIRE(candidate.diagnostics().nonFiniteSamples == 0);
		vekt::audio_analysis::SinusoidalProjector fundamental(rate, inputHz, window);
		for (const auto value : host) fundamental.add(value);
		REQUIRE(fundamental.peakAmplitude() > 1.0e-5);
		const auto coherentDbc = [&](const std::vector<double>& samples, int hz)
		{
			vekt::audio_analysis::SinusoidalProjector main(rate, inputHz, window);
			vekt::audio_analysis::SinusoidalProjector spur(rate, hz, window);
			for (const auto value : samples) { main.add(value); spur.add(value); }
			return 20.0 * std::log10(std::max(spur.peakAmplitude(), 1.0e-15) / main.peakAmplitude());
		};
		for (const auto hz : { 1'000, 5'000 })
		{
			const auto candidateDbc = coherentDbc(host, hz);
			const auto filtered16Dbc = coherentDbc(reference16, hz);
			const auto filtered32Dbc = coherentDbc(reference32, hz);
			const auto referenceBinChangeDb = std::abs(filtered16Dbc - filtered32Dbc);
			WARN("1x filtered reference: drive=" << drive << " hz=" << hz
				<< " one_step_dBc=" << candidateDbc << " filtered16_dBc=" << filtered16Dbc
				<< " filtered32_dBc=" << filtered32Dbc << " reference_bin_change_dB="
				<< referenceBinChangeDb);
			CAPTURE(drive, hz, candidateDbc, filtered16Dbc, filtered32Dbc, referenceBinChangeDb);
			REQUIRE(std::isfinite(candidateDbc));
			REQUIRE(std::isfinite(filtered16Dbc));
			REQUIRE(std::isfinite(filtered32Dbc));
			// A two-factor check for these bins only, not render convergence.
			REQUIRE(referenceBinChangeDb < 0.5);
			REQUIRE(candidateDbc > filtered32Dbc + 20.0);
		}
	}
}

TEST_CASE("One-times default candidate audits coherent bins against unfiltered reference returns", "[audio-lab][kobber][ladder-default-feasibility][slow]")
{
	constexpr double rate = 48'000.0;
	constexpr int window = 4'800;
	for (const auto resonance : { 0.5f, 0.98f })
		for (const auto drive : { 12.0f, 24.0f })
		{
			std::array<vekt::audio_lab::NonlinearTptLadder, 3> candidates;
			vekt::audio_lab::NonlinearTptLadderOfflineReference reference16, reference32;
			for (auto& candidate : candidates) candidate.prepare(rate);
			reference16.prepare(rate, 16);
			reference32.prepare(rate, 32);
			std::array<std::vector<vekt::audio_analysis::SinusoidalProjector>, 5> bins;
			for (auto& path : bins)
				for (int frequency = 1'000; frequency < static_cast<int>(rate / 2.0); frequency += 1'000)
					path.emplace_back(rate, static_cast<double>(frequency), window);
			for (int sample = 0; sample < 2 * window; ++sample)
			{
				const auto input = 0.5f * static_cast<float>(std::sin(
					2.0 * std::numbers::pi * 7.0 * sample / 48.0));
				const auto settings = vekt::audio_lab::NonlinearTptLadderSettings { 10'000.0f, resonance, drive };
				const std::array output {
					static_cast<double>(candidates[0].process(input, settings)),
					static_cast<double>(candidates[1].processSubstepped(input, settings, 2)),
					static_cast<double>(candidates[2].processSubstepped(input, settings, 4)),
					reference16.process(input, { 10'000.0, resonance, drive }),
					reference32.process(input, { 10'000.0, resonance, drive })
				};
				if (sample >= window)
					for (std::size_t path = 0; path < bins.size(); ++path)
						for (auto& bin : bins[path]) bin.add(output[path]);
			}
			std::array<double, std::tuple_size_v<decltype(bins)>> fundamentals {}; // candidates, then both references
			for (std::size_t path = 0; path < bins.size(); ++path)
			{
				fundamentals[path] = bins[path][6].peakAmplitude();
				REQUIRE(fundamentals[path] > 1.0e-5);
			}
			std::array<int, 3> largestFrequency {};
			std::array<double, 3> largestCandidatePeak {};
			// This reference returns one sample per host interval without a
			// reconstruction low-pass; its host bins are integration diagnostics,
			// not an unaliased ground truth or an audibility criterion.
			for (int frequency = 1'000; frequency < static_cast<int>(rate / 2.0); frequency += 1'000)
			{
				if (frequency % 7'000 == 0) continue; // Direct in-band harmonics.
				for (std::size_t path = 0; path < candidates.size(); ++path)
					if (const auto peak = bins[path][static_cast<std::size_t>(frequency / 1'000 - 1)].peakAmplitude();
						peak > largestCandidatePeak[path])
					{
						largestCandidatePeak[path] = peak;
						largestFrequency[path] = frequency;
					}
			}
			const auto db = [&](std::size_t path, std::size_t selected)
			{
				return 20.0 * std::log10(std::max(bins[path][selected].peakAmplitude(), 1.0e-15)
					/ fundamentals[path]);
			};
			for (std::size_t path = 0; path < candidates.size(); ++path)
			{
				REQUIRE(largestFrequency[path] != 0);
				const auto selected = static_cast<std::size_t>(largestFrequency[path] / 1'000 - 1);
				WARN("1x raw host-band diagnostic: resonance=" << resonance << " drive=" << drive
					<< " steps=" << (1 << path) << " largest_candidate_hz=" << largestFrequency[path]
					<< " candidate_dBc=" << db(path, selected)
					<< " reference16_same_bin_dBc=" << db(3, selected)
					<< " reference32_same_bin_dBc=" << db(4, selected));
				CAPTURE(resonance, drive, path, largestFrequency[path], fundamentals[path],
					db(path, selected), db(3, selected), db(4, selected));
				REQUIRE(std::isfinite(db(path, selected)));
				REQUIRE(std::isfinite(db(3, selected)));
				REQUIRE(std::isfinite(db(4, selected)));
				REQUIRE(candidates[path].diagnostics().samples == static_cast<std::uint64_t>(2 * window * (1 << path)));
				REQUIRE(candidates[path].diagnostics().unconvergedSamples == 0);
				REQUIRE(candidates[path].diagnostics().nonFiniteSamples == 0);
			}
			REQUIRE(reference16.diagnostics().unconvergedSteps == 0);
			REQUIRE(reference32.diagnostics().unconvergedSteps == 0);
			REQUIRE(reference16.diagnostics().nonFiniteSteps == 0);
			REQUIRE(reference32.diagnostics().nonFiniteSteps == 0);
		}
}

TEST_CASE("Bounded candidate substeps track the independent reference", "[audio-lab][kobber][ladder-feasibility]")
{
	for (const auto rate : { 48'000.0, 96'000.0 })
	for (const auto steps : { 2, 4 })
	{
		vekt::audio_lab::NonlinearTptLadder candidate;
		vekt::audio_lab::NonlinearTptLadderOfflineReference reference;
		candidate.prepare(rate);
		reference.prepare(rate, steps);
		for (int sample = 0; sample < 4'096; ++sample)
		{
			const auto time = sample / rate;
			const auto input = 0.5f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 7'000.0 * time));
			const auto cutoff = 10'000.0f + 1'000.0f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 37.0 * time));
			const auto resonance = 0.85f + 0.13f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 23.0 * time));
			const auto drive = 12.0f + 6.0f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 41.0 * time));
			const auto actual = candidate.processSubstepped(input, { cutoff, resonance, drive }, steps);
			const auto expected = reference.process(input, { cutoff, resonance, drive });
			CAPTURE(rate, steps, sample, actual, expected);
			REQUIRE(std::isfinite(actual));
			REQUIRE(static_cast<double>(actual) == Catch::Approx(expected).margin(2.0e-5));
		}
		REQUIRE(candidate.diagnostics().samples == static_cast<std::uint64_t>(steps) * 4'096);
		REQUIRE(candidate.diagnostics().unconvergedSamples == 0);
		REQUIRE(candidate.diagnostics().nonFiniteSamples == 0);
		REQUIRE(reference.diagnostics().unconvergedSteps == 0);
		REQUIRE(reference.diagnostics().nonFiniteSteps == 0);
	}
}

TEST_CASE("Bounded candidate substeps preserve block independence and fresh rerenders", "[audio-lab][kobber][ladder-feasibility]")
{
	using namespace vekt::dsp;
	constexpr int sampleCount = 512;
	const auto render = [](int blockSize)
	{
		OversamplingBank<float> bank(1);
		bank.prepare(127);
		bank.activate({ OversamplingFactor::x2, OversamplingFilter::polyphaseIIR });
		vekt::audio_lab::NonlinearTptLadder candidate;
		candidate.prepare(96'000.0);
		juce::AudioBuffer<float> buffer(1, 127);
		std::array<float, sampleCount> output {};
		for (int start = 0; start < sampleCount; start += blockSize)
		{
			const auto count = std::min(blockSize, sampleCount - start);
			for (int index = 0; index < count; ++index)
				buffer.setSample(0, index, 0.5f * static_cast<float>(std::sin(
					2.0 * std::numbers::pi * 7'000.0 * (start + index) / 48'000.0)));
			juce::dsp::AudioBlock<float> full(buffer);
			auto host = full.getSubBlock(0, static_cast<std::size_t>(count));
			const juce::dsp::AudioBlock<const float> input(host);
			auto internal = bank.processSamplesUp(input);
			for (std::size_t index = 0; index < internal.getNumSamples(); ++index)
			{
				const auto time = (2.0 * start + static_cast<double>(index)) / 96'000.0;
				const auto cutoff = 10'000.0f + 1'000.0f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 37.0 * time));
				internal.setSample(0, static_cast<int>(index), candidate.processSubstepped(
					internal.getSample(0, static_cast<int>(index)), { cutoff, 0.98f, 12.0f }, 4));
			}
			bank.processSamplesDown(host);
			for (int index = 0; index < count; ++index)
				output[static_cast<std::size_t>(start + index)] = buffer.getSample(0, index);
		}
		REQUIRE(candidate.diagnostics().samples == 4 * 2 * sampleCount);
		REQUIRE(candidate.diagnostics().unconvergedSamples == 0);
		REQUIRE(candidate.diagnostics().nonFiniteSamples == 0);
		return output;
	};
	const auto baseline = render(127);
	const auto single = render(1);
	const auto repeat = render(127);
	for (int index = 0; index < sampleCount; ++index)
	{
		CAPTURE(index);
		REQUIRE(std::isfinite(baseline[static_cast<std::size_t>(index)]));
		REQUIRE(std::bit_cast<std::uint32_t>(baseline[static_cast<std::size_t>(index)])
			== std::bit_cast<std::uint32_t>(single[static_cast<std::size_t>(index)]));
		REQUIRE(std::bit_cast<std::uint32_t>(baseline[static_cast<std::size_t>(index)])
			== std::bit_cast<std::uint32_t>(repeat[static_cast<std::size_t>(index)]));
	}
}

TEST_CASE("Bounded candidate substeps reduce the known 2x host-band spurs", "[audio-lab][kobber][ladder-feasibility]")
{
	using namespace vekt::dsp;
	constexpr double rate = 48'000.0;
	constexpr int window = 4'800, blockSize = 96;
	for (const auto drive : { 12.0f, 24.0f })
	{
		std::array<double, 3> largestDbc {};
		for (int path = 0; path < 3; ++path)
		{
			OversamplingBank<float> bank(1);
			bank.prepare(blockSize);
			bank.activate({ OversamplingFactor::x2, OversamplingFilter::polyphaseIIR });
			vekt::audio_lab::NonlinearTptLadder candidate;
			candidate.prepare(rate * 2.0);
			vekt::audio_analysis::SinusoidalProjector fundamental(rate, 7'000.0, window);
			std::vector<vekt::audio_analysis::SinusoidalProjector> bins;
			for (int bin = 1; bin < 24; ++bin)
				bins.emplace_back(rate, rate * bin / 48.0, window);
			juce::AudioBuffer<float> buffer(1, blockSize);
			for (int start = 0; start < 2 * window; start += blockSize)
			{
				for (int index = 0; index < blockSize; ++index)
					buffer.setSample(0, index, 0.5f * static_cast<float>(std::sin(
						2.0 * std::numbers::pi * 7.0 * (start + index) / 48.0)));
				juce::dsp::AudioBlock<float> host(buffer);
				const juce::dsp::AudioBlock<const float> input(host);
				auto internal = bank.processSamplesUp(input);
				for (std::size_t index = 0; index < internal.getNumSamples(); ++index)
				{
					const auto sample = internal.getSample(0, static_cast<int>(index));
					internal.setSample(0, static_cast<int>(index), path == 0
						? candidate.process(sample, { 10'000.0f, 0.98f, drive })
						: candidate.processSubstepped(sample, { 10'000.0f, 0.98f, drive }, path == 1 ? 2 : 4));
				}
				bank.processSamplesDown(host);
				if (start >= window)
					for (int index = 0; index < blockSize; ++index)
					{
						fundamental.add(buffer.getSample(0, index));
						for (auto& bin : bins) bin.add(buffer.getSample(0, index));
					}
			}
			REQUIRE(fundamental.peakAmplitude() > 1.0e-5);
			int largestBin {};
			double largestPeak {};
			for (int bin = 1; bin < 24; ++bin)
			{
				if (bin == 7 || bin == 14 || bin == 21) continue;
				if (const auto peak = bins[static_cast<std::size_t>(bin - 1)].peakAmplitude(); peak > largestPeak)
				{
					largestPeak = peak;
					largestBin = bin;
				}
			}
			largestDbc[static_cast<std::size_t>(path)] = 20.0 * std::log10(
				std::max(largestPeak, 1.0e-15) / fundamental.peakAmplitude());
			WARN("bounded candidate 2x: drive=" << drive << " steps=" << (path == 0 ? 1 : path == 1 ? 2 : 4)
				<< " largest_hz=" << largestBin * 1'000 << " largest_dBc="
				<< largestDbc[static_cast<std::size_t>(path)]);
			REQUIRE(std::isfinite(largestDbc[static_cast<std::size_t>(path)]));
			REQUIRE(candidate.diagnostics().unconvergedSamples == 0);
			REQUIRE(candidate.diagnostics().nonFiniteSamples == 0);
		}
		REQUIRE(largestDbc[1] < largestDbc[0] - (drive == 12.0f ? 20.0 : 10.0));
		REQUIRE(largestDbc[2] < largestDbc[0] - 25.0);
	}
}

TEST_CASE("Nonlinear TPT ladder 2x substep spur reaches the host output", "[audio-lab][kobber][ladder-internal-alias][slow]")
{
	using namespace vekt::dsp;
	constexpr double rate = 48'000.0;
	constexpr int window = 4'800, blockSize = 96;
	for (const auto filter : { OversamplingFilter::polyphaseIIR, OversamplingFilter::polyphaseFIR })
	{
		std::array<double, 3> spurDbc {};
		for (std::size_t path = 0; path < spurDbc.size(); ++path)
		{
			OversamplingBank<float> bank(1);
			bank.prepare(blockSize);
			bank.activate({ OversamplingFactor::x2, filter });
			vekt::audio_lab::NonlinearTptLadder candidate;
			vekt::audio_lab::NonlinearTptLadderOfflineReference reference;
			candidate.prepare(rate * 2.0);
			reference.prepare(rate * 2.0, path == 1 ? 4 : 32);
			vekt::audio_analysis::SinusoidalProjector fundamental(rate, 7'000.0, window);
			vekt::audio_analysis::SinusoidalProjector spur(rate, 5'000.0, window);
			juce::AudioBuffer<float> buffer(1, blockSize);
			for (int start = 0; start < 2 * window; start += blockSize)
			{
				for (int index = 0; index < blockSize; ++index)
					buffer.setSample(0, index, 0.5f * static_cast<float>(std::sin(
						2.0 * std::numbers::pi * 7.0 * (start + index) / 48.0)));
				juce::dsp::AudioBlock<float> host(buffer);
				const juce::dsp::AudioBlock<const float> input(host);
				auto internal = bank.processSamplesUp(input);
				for (std::size_t index = 0; index < internal.getNumSamples(); ++index)
				{
					const auto sample = internal.getSample(0, static_cast<int>(index));
					internal.setSample(0, static_cast<int>(index), path == 0
						? candidate.process(sample, { 10'000.0f, 0.98f, 12.0f })
						: static_cast<float>(reference.process(sample, { 10'000.0, 0.98, 12.0 })));
				}
				bank.processSamplesDown(host);
				if (start >= window)
					for (int index = 0; index < blockSize; ++index)
					{
						fundamental.add(buffer.getSample(0, index));
						spur.add(buffer.getSample(0, index));
					}
			}
			REQUIRE(fundamental.peakAmplitude() > 1.0e-5);
			spurDbc[path] = 20.0 * std::log10(std::max(spur.peakAmplitude(), 1.0e-15)
				/ fundamental.peakAmplitude());
			WARN("host 2x 5 kHz: filter=" << (filter == OversamplingFilter::polyphaseIIR ? "IIR" : "FIR")
				<< " substeps=" << (path == 0 ? 0 : path == 1 ? 4 : 32)
				<< " dBc=" << spurDbc[path]);
			REQUIRE(std::isfinite(spurDbc[path]));
			REQUIRE(candidate.diagnostics().unconvergedSamples == 0);
			REQUIRE(reference.diagnostics().unconvergedSteps == 0);
			REQUIRE(reference.diagnostics().nonFiniteSteps == 0);
		}
		REQUIRE(spurDbc[1] < spurDbc[0] - 25.0);
		REQUIRE(spurDbc[2] < spurDbc[0] - 25.0);
	}
}

TEST_CASE("Nonlinear TPT ladder 2x substeps audit the settled host band", "[audio-lab][kobber][ladder-substep-spectrum][slow]")
{
	using namespace vekt::dsp;
	constexpr double rate = 48'000.0;
	constexpr int window = 4'800, blockSize = 96;
	for (const auto filter : { OversamplingFilter::polyphaseIIR, OversamplingFilter::polyphaseFIR })
		for (const auto drive : { 12.0f, 24.0f })
		{
			std::array<double, 3> largestDbc {};
			for (std::size_t path = 0; path < largestDbc.size(); ++path)
			{
				OversamplingBank<float> bank(1);
				bank.prepare(blockSize);
				bank.activate({ OversamplingFactor::x2, filter });
				vekt::audio_lab::NonlinearTptLadder candidate;
				vekt::audio_lab::NonlinearTptLadderOfflineReference reference;
				candidate.prepare(rate * 2.0);
				reference.prepare(rate * 2.0, path == 1 ? 4 : 16);
				juce::AudioBuffer<float> buffer(1, blockSize);
				std::vector<vekt::audio_analysis::SinusoidalProjector> bins;
				for (int bin = 1; bin < 24; ++bin)
					bins.emplace_back(rate, rate * bin / 48.0, window);
				bool finite = true;
				for (int start = 0; start < 2 * window; start += blockSize)
				{
					for (int index = 0; index < blockSize; ++index)
						buffer.setSample(0, index, 0.5f * static_cast<float>(std::sin(
							2.0 * std::numbers::pi * 7.0 * (start + index) / 48.0)));
					juce::dsp::AudioBlock<float> host(buffer);
					const juce::dsp::AudioBlock<const float> input(host);
					auto internal = bank.processSamplesUp(input);
					for (std::size_t index = 0; index < internal.getNumSamples(); ++index)
					{
						const auto sample = internal.getSample(0, static_cast<int>(index));
						internal.setSample(0, static_cast<int>(index), path == 0
							? candidate.process(sample, { 10'000.0f, 0.98f, drive })
							: static_cast<float>(reference.process(sample, { 10'000.0, 0.98, drive })));
					}
					bank.processSamplesDown(host);
					if (start >= window)
						for (int index = 0; index < blockSize; ++index)
						{
							const auto output = buffer.getSample(0, index);
							finite = finite && std::isfinite(output);
							for (auto& bin : bins) bin.add(output);
						}
				}
				REQUIRE(finite);
				const auto fundamental = bins[6].peakAmplitude();
				REQUIRE(fundamental > 1.0e-5);
				int largestBin {};
				double largestPeak {};
				for (int bin = 1; bin < 24; ++bin)
				{
					// Intentional in-band harmonics are not unexpected spurs.
					if (bin == 7 || bin == 14 || bin == 21) continue;
					if (const auto peak = bins[static_cast<std::size_t>(bin - 1)].peakAmplitude(); peak > largestPeak)
					{
						largestPeak = peak;
						largestBin = bin;
					}
				}
				largestDbc[path] = 20.0 * std::log10(std::max(largestPeak, 1.0e-15) / fundamental);
				const auto fifthDbc = 20.0 * std::log10(std::max(bins[4].peakAmplitude(), 1.0e-15) / fundamental);
				WARN("2x host-band substep audit: filter=" << (filter == OversamplingFilter::polyphaseIIR ? "IIR" : "FIR")
					<< " drive=" << drive << " substeps=" << (path == 0 ? 0 : path == 1 ? 4 : 16)
					<< " largest_bin=" << largestBin << " largest_dBc=" << largestDbc[path]
					<< " 5k_dBc=" << fifthDbc);
				REQUIRE(std::isfinite(largestDbc[path]));
				REQUIRE(candidate.diagnostics().unconvergedSamples == 0);
				REQUIRE(candidate.diagnostics().nonFiniteSamples == 0);
				REQUIRE(reference.diagnostics().unconvergedSteps == 0);
				REQUIRE(reference.diagnostics().nonFiniteSteps == 0);
			}
			// A regression for this exact stimulus, not a general alias specification.
			REQUIRE(largestDbc[1] < largestDbc[0]);
			REQUIRE(largestDbc[2] < largestDbc[0]);
			REQUIRE(largestDbc[1] < (drive == 12.0f ? -75.0 : -55.0));
		}
}

TEST_CASE("Nonlinear TPT ladder 2x IIR substeps audit normalized host rates", "[audio-lab][kobber][ladder-substep-rate-matrix][slow]")
{
	using namespace vekt::dsp;
	constexpr int window = 4'800, blockSize = 96;
	for (const auto rate : { 44'100.0, 48'000.0, 88'200.0, 96'000.0, 192'000.0 })
		for (const auto drive : { 12.0f, 24.0f })
			for (const auto resonance : { 0.5f, 0.98f })
			{
				std::array<double, 2> largestDbc {};
				std::array<int, 2> largestBins {};
				for (std::size_t path = 0; path < largestDbc.size(); ++path)
				{
					OversamplingBank<float> bank(1);
					bank.prepare(blockSize);
					bank.activate({ OversamplingFactor::x2, OversamplingFilter::polyphaseIIR });
					vekt::audio_lab::NonlinearTptLadder candidate;
					vekt::audio_lab::NonlinearTptLadderOfflineReference reference;
					candidate.prepare(rate * 2.0);
					reference.prepare(rate * 2.0, 4);
					juce::AudioBuffer<float> buffer(1, blockSize);
					std::vector<vekt::audio_analysis::SinusoidalProjector> bins;
					for (int bin = 1; bin < 24; ++bin)
						bins.emplace_back(rate, rate * bin / 48.0, window);
					bool finite = true;
					for (int start = 0; start < 2 * window; start += blockSize)
					{
						for (int index = 0; index < blockSize; ++index)
							buffer.setSample(0, index, 0.5f * static_cast<float>(std::sin(
								2.0 * std::numbers::pi * 7.0 * (start + index) / 48.0)));
						juce::dsp::AudioBlock<float> host(buffer);
						const juce::dsp::AudioBlock<const float> input(host);
						auto internal = bank.processSamplesUp(input);
						for (std::size_t index = 0; index < internal.getNumSamples(); ++index)
						{
							const auto sample = internal.getSample(0, static_cast<int>(index));
							internal.setSample(0, static_cast<int>(index), path == 0
								? candidate.process(sample, { static_cast<float>(rate * 10.0 / 48.0), resonance, drive })
								: static_cast<float>(reference.process(sample, { rate * 10.0 / 48.0, resonance, drive })));
						}
						bank.processSamplesDown(host);
						if (start >= window)
							for (int index = 0; index < blockSize; ++index)
							{
								const auto output = buffer.getSample(0, index);
								finite = finite && std::isfinite(output);
								for (auto& bin : bins) bin.add(output);
							}
					}
					REQUIRE(finite);
					const auto fundamental = bins[6].peakAmplitude();
					REQUIRE(fundamental > 1.0e-5);
					double maximumPeak {};
					for (int bin = 1; bin < 24; ++bin)
					{
						if (bin == 7 || bin == 14 || bin == 21) continue;
						if (const auto peak = bins[static_cast<std::size_t>(bin - 1)].peakAmplitude(); peak > maximumPeak)
						{
							maximumPeak = peak;
							largestBins[path] = bin;
						}
					}
					largestDbc[path] = 20.0 * std::log10(std::max(maximumPeak, 1.0e-15) / fundamental);
					REQUIRE(std::isfinite(largestDbc[path]));
					REQUIRE(candidate.diagnostics().unconvergedSamples == 0);
					REQUIRE(candidate.diagnostics().nonFiniteSamples == 0);
					REQUIRE(reference.diagnostics().unconvergedSteps == 0);
					REQUIRE(reference.diagnostics().nonFiniteSteps == 0);
				}
				WARN("2x IIR normalized rate matrix: rate=" << rate << " drive=" << drive
					<< " resonance=" << resonance << " candidate_bin=" << largestBins[0]
					<< " candidate_dBc=" << largestDbc[0] << " reference_bin=" << largestBins[1]
					<< " four_substeps_dBc=" << largestDbc[1]);
				// Do not infer an absolute-frequency or modulation specification.
				REQUIRE(largestDbc[1] < largestDbc[0]);
			}
}

TEST_CASE("Nonlinear TPT ladder 2x IIR fixed-frequency spur changes across host rates", "[audio-lab][kobber][ladder-substep-absolute-rates][slow]")
{
	using namespace vekt::dsp;
	constexpr int blockSize = 30;
	for (const auto rate : { 44'100.0, 48'000.0, 88'200.0, 96'000.0, 192'000.0 })
	{
		const auto window = static_cast<int>(rate / 10.0);
		std::array<double, 2> maximumDbc {};
		for (const auto substeps : { 0, 4 })
		{
			OversamplingBank<float> bank(1);
			bank.prepare(blockSize);
			bank.activate({ OversamplingFactor::x2, OversamplingFilter::polyphaseIIR });
			vekt::audio_lab::NonlinearTptLadder candidate;
			vekt::audio_lab::NonlinearTptLadderOfflineReference reference;
			candidate.prepare(rate * 2.0);
			reference.prepare(rate * 2.0, 4);
			juce::AudioBuffer<float> buffer(1, blockSize);
			vekt::audio_analysis::SinusoidalProjector fundamental(rate, 7'000.0,
				static_cast<std::size_t>(window));
			vekt::audio_analysis::SinusoidalProjector spur5k(rate, 5'000.0,
				static_cast<std::size_t>(window));
			vekt::audio_analysis::SinusoidalProjector spur2800(rate, 2'800.0,
				static_cast<std::size_t>(window));
			std::vector<float> settled;
			settled.reserve(static_cast<std::size_t>(window));
			for (int start = 0; start < 2 * window; start += blockSize)
			{
				for (int index = 0; index < blockSize; ++index)
					buffer.setSample(0, index, 0.5f * static_cast<float>(std::sin(
						2.0 * std::numbers::pi * 7'000.0 * (start + index) / rate)));
				juce::dsp::AudioBlock<float> host(buffer);
				const juce::dsp::AudioBlock<const float> input(host);
				auto internal = bank.processSamplesUp(input);
				for (std::size_t index = 0; index < internal.getNumSamples(); ++index)
				{
					const auto sample = internal.getSample(0, static_cast<int>(index));
					internal.setSample(0, static_cast<int>(index), substeps == 0
						? candidate.process(sample, { 10'000.0f, 0.98f, 12.0f })
						: static_cast<float>(reference.process(sample, { 10'000.0, 0.98, 12.0 })));
				}
				bank.processSamplesDown(host);
				if (start >= window)
					for (int index = 0; index < blockSize; ++index)
					{
						const auto output = buffer.getSample(0, index);
						settled.push_back(output);
						fundamental.add(output);
						spur5k.add(output);
						spur2800.add(output);
					}
			}
			const auto main = fundamental.peakAmplitude();
			REQUIRE(main > 1.0e-5);
			const auto fifth = 20.0 * std::log10(std::max(spur5k.peakAmplitude(), 1.0e-15) / main);
			const auto fold2800 = 20.0 * std::log10(std::max(spur2800.peakAmplitude(), 1.0e-15) / main);
			REQUIRE(settled.size() == static_cast<std::size_t>(window));
			REQUIRE(std::all_of(settled.begin(), settled.end(), [](float value) { return std::isfinite(value); }));
			// The 0.1 s settled window is coherent at 100 Hz increments. Scan
			// every such bin below Nyquist, excluding direct in-band 7 kHz harmonics.
			double maximumPeak {};
			int maximumFrequency {};
			for (int frequency = 100; frequency < static_cast<int>(rate / 2.0); frequency += 100)
			{
				if (frequency % 7'000 == 0) continue;
				const auto phaseStep = 2.0 * std::numbers::pi * frequency / rate;
				const auto stepReal = std::cos(phaseStep), stepImaginary = -std::sin(phaseStep);
				double oscillatorReal = 1.0, oscillatorImaginary = 0.0;
				double projectionReal = 0.0, projectionImaginary = 0.0;
				for (const auto sample : settled)
				{
					projectionReal += sample * oscillatorReal;
					projectionImaginary += sample * oscillatorImaginary;
					const auto nextReal = oscillatorReal * stepReal - oscillatorImaginary * stepImaginary;
					oscillatorImaginary = oscillatorReal * stepImaginary + oscillatorImaginary * stepReal;
					oscillatorReal = nextReal;
				}
				const auto peak = 2.0 * std::hypot(projectionReal, projectionImaginary)
					/ static_cast<double>(window);
				if (peak > maximumPeak) { maximumPeak = peak; maximumFrequency = frequency; }
			}
			const auto largestDbc = 20.0 * std::log10(std::max(maximumPeak, 1.0e-15) / main);
			maximumDbc[substeps == 0 ? 0 : 1] = largestDbc;
			if (rate == 44'100.0)
			{
				REQUIRE(maximumFrequency == 2'800);
				REQUIRE(std::abs(largestDbc - fold2800) < 0.01);
			}
			if (rate == 48'000.0)
			{
				REQUIRE(maximumFrequency == 5'000);
				REQUIRE(std::abs(largestDbc - fifth) < 0.01);
			}
			WARN("2x fixed-frequency audit: rate=" << rate << " substeps=" << substeps
				<< " largest_hz=" << maximumFrequency << " largest_dBc=" << largestDbc
				<< " 5k_dBc=" << fifth << " 2800_dBc=" << fold2800);
			REQUIRE(std::isfinite(largestDbc));
			REQUIRE(std::isfinite(fifth));
			REQUIRE(std::isfinite(fold2800));
			REQUIRE(candidate.diagnostics().unconvergedSamples == 0);
			REQUIRE(reference.diagnostics().unconvergedSteps == 0);
			REQUIRE(reference.diagnostics().nonFiniteSteps == 0);
		}
		WARN("2x fixed-frequency maximum comparison: rate=" << rate << " candidate_dBc="
			<< maximumDbc[0] << " four_substeps_dBc=" << maximumDbc[1]);
		// Bounds on this fixed-tone experiment, not a product alias specification.
		if (rate <= 48'000.0) REQUIRE(maximumDbc[1] < maximumDbc[0] - 25.0);
	}
}

TEST_CASE("Nonlinear TPT ladder 96 kHz 47 kHz spur checks offline substep convergence", "[audio-lab][kobber][ladder-substep-96k][slow]")
{
	using namespace vekt::dsp;
	constexpr double rate = 96'000.0;
	constexpr int window = 9'600, blockSize = 96;
	std::array<double, 4> spurDbc {};
	std::array<double, 4> seventhDbcByPath {};
	std::size_t path {};
	for (const auto substeps : { 0, 4, 16, 32 })
	{
		OversamplingBank<float> bank(1);
		bank.prepare(blockSize);
		bank.activate({ OversamplingFactor::x2, OversamplingFilter::polyphaseIIR });
		vekt::audio_lab::NonlinearTptLadder candidate;
		vekt::audio_lab::NonlinearTptLadderOfflineReference reference;
		candidate.prepare(rate * 2.0);
		reference.prepare(rate * 2.0, std::max(1, substeps));
		juce::AudioBuffer<float> buffer(1, blockSize);
		vekt::audio_analysis::SinusoidalProjector fundamental(rate, 7'000.0, window);
		vekt::audio_analysis::SinusoidalProjector spur(rate, 47'000.0, window);
		vekt::audio_analysis::SinusoidalProjector internalFundamental(rate * 2.0, 7'000.0, window * 2);
		vekt::audio_analysis::SinusoidalProjector internalSeventh(rate * 2.0, 49'000.0, window * 2);
		vekt::audio_analysis::SinusoidalProjector internal47k(rate * 2.0, 47'000.0, window * 2);
		for (int start = 0; start < 2 * window; start += blockSize)
		{
			for (int index = 0; index < blockSize; ++index)
				buffer.setSample(0, index, 0.5f * static_cast<float>(std::sin(
					2.0 * std::numbers::pi * 7'000.0 * (start + index) / rate)));
			juce::dsp::AudioBlock<float> host(buffer);
			const juce::dsp::AudioBlock<const float> input(host);
			auto internal = bank.processSamplesUp(input);
			for (std::size_t index = 0; index < internal.getNumSamples(); ++index)
			{
				const auto sample = internal.getSample(0, static_cast<int>(index));
				internal.setSample(0, static_cast<int>(index), substeps == 0
					? candidate.process(sample, { 10'000.0f, 0.98f, 12.0f })
					: static_cast<float>(reference.process(sample, { 10'000.0, 0.98, 12.0 })));
			}
			if (start >= window)
				for (std::size_t index = 0; index < internal.getNumSamples(); ++index)
				{
					const auto output = internal.getSample(0, static_cast<int>(index));
					internalFundamental.add(output);
					internalSeventh.add(output);
					internal47k.add(output);
				}
			bank.processSamplesDown(host);
			if (start >= window)
				for (int index = 0; index < blockSize; ++index)
				{
					const auto output = buffer.getSample(0, index);
					fundamental.add(output);
					spur.add(output);
				}
		}
		const auto main = fundamental.peakAmplitude();
		REQUIRE(main > 1.0e-5);
		REQUIRE(internalFundamental.peakAmplitude() > 1.0e-5);
		const auto seventhDbc = 20.0 * std::log10(std::max(internalSeventh.peakAmplitude(), 1.0e-15)
			/ internalFundamental.peakAmplitude());
		const auto internal47Dbc = 20.0 * std::log10(std::max(internal47k.peakAmplitude(), 1.0e-15)
			/ internalFundamental.peakAmplitude());
		seventhDbcByPath[path] = seventhDbc;
		spurDbc[path++] = 20.0 * std::log10(std::max(spur.peakAmplitude(), 1.0e-15) / main);
		WARN("96k host 47k: substeps=" << substeps << " dBc=" << spurDbc[path - 1]
			<< " internal_49k_dBc=" << seventhDbc << " internal_47k_dBc=" << internal47Dbc
			<< " reference_max_residual=" << reference.diagnostics().maximumResidual);
		REQUIRE(std::isfinite(seventhDbc));
		REQUIRE(std::isfinite(internal47Dbc));
		// The host fold follows 49 kHz before decimation, not the internal 47 kHz bin.
		REQUIRE(seventhDbc > internal47Dbc + 50.0);
		REQUIRE(std::abs((spurDbc[path - 1] - seventhDbc) + 10.5) < 1.0);
		REQUIRE(std::isfinite(spurDbc[path - 1]));
		REQUIRE(candidate.diagnostics().unconvergedSamples == 0);
		REQUIRE(candidate.diagnostics().nonFiniteSamples == 0);
		REQUIRE(reference.diagnostics().unconvergedSteps == 0);
		REQUIRE(reference.diagnostics().nonFiniteSteps == 0);
	}
	// The single coherent bin is not a convergence proof for the full render.
	REQUIRE(spurDbc[1] > spurDbc[0] + 2.0);
	REQUIRE(std::abs(spurDbc[3] - spurDbc[2]) < 0.1);
	REQUIRE(std::abs((spurDbc[1] - spurDbc[0]) - (seventhDbcByPath[1] - seventhDbcByPath[0])) < 0.1);
}

TEST_CASE("Nonlinear TPT ladder high-cutoff coherent spurs are compared with substep references", "[audio-lab][kobber][ladder-spur-reference][slow]")
{
	constexpr double rate = 48'000.0;
	constexpr int window = 4'800;
	std::vector<double> fifthDbc;
	for (const int substeps : { 16, 32, 64, 128 })
	{
		vekt::audio_lab::NonlinearTptLadderOfflineReference reference;
		reference.prepare(rate, substeps);
		vekt::audio_analysis::SinusoidalProjector fundamental(rate, 7'000.0, window);
		vekt::audio_analysis::SinusoidalProjector spur5k(rate, 5'000.0, window);
		vekt::audio_analysis::SinusoidalProjector spur3k(rate, 3'000.0, window);
		for (int sample = 0; sample < 2 * window; ++sample)
		{
			const auto input = 0.5f * static_cast<float>(std::sin(
				2.0 * std::numbers::pi * 7.0 * sample / 48.0));
			const auto output = reference.process(input, { 10'000.0, 0.98, 12.0 });
			if (sample >= window)
			{
				fundamental.add(output);
				spur5k.add(output);
				spur3k.add(output);
			}
		}
		const auto main = fundamental.peakAmplitude();
		const auto fifth = 20.0 * std::log10(std::max(spur5k.peakAmplitude(), 1.0e-15) / main);
		const auto third = 20.0 * std::log10(std::max(spur3k.peakAmplitude(), 1.0e-15) / main);
		WARN("high-cutoff reference substeps=" << substeps << " 5k_dBc=" << fifth
			<< " 3k_dBc=" << third << " fundamental=" << main);
		CAPTURE(substeps, fifth, third, reference.diagnostics().maximumResidual);
		REQUIRE(main > 1.0e-5);
		REQUIRE(std::isfinite(fifth));
		REQUIRE(std::isfinite(third));
		REQUIRE(reference.diagnostics().unconvergedSteps == 0);
		REQUIRE(reference.diagnostics().nonFiniteSteps == 0);
		REQUIRE(reference.diagnostics().maximumResidual <= 1.0e-13);
		fifthDbc.push_back(fifth);
	}
	REQUIRE(std::abs(fifthDbc[3] - fifthDbc[2]) < 0.05);
}

TEST_CASE("Nonlinear TPT ladder coherent IMD agrees with a converged offline reference", "[audio-lab][kobber][ladder-imd][slow]")
{
	constexpr double rate = 48'000.0;
	constexpr int settle = 12'000, length = 24'000;
	constexpr double firstHz = 300.0, secondHz = 500.0;
	vekt::audio_lab::NonlinearTptLadder candidate;
	candidate.prepare(rate);
	vekt::audio_lab::NonlinearTptLadderOfflineReference reference16, reference32;
	reference16.prepare(rate, 16);
	reference32.prepare(rate, 32);
	std::vector<float> candidateSamples(length);
	std::vector<double> samples16(length), samples32(length);
	for (int sample = 0; sample < settle + length; ++sample)
	{
		const auto time = static_cast<double>(sample) / rate;
		const auto input = 0.25f * static_cast<float>(
			std::sin(2.0 * std::numbers::pi * firstHz * time)
			+ std::sin(2.0 * std::numbers::pi * secondHz * time));
		const auto candidateOutput = candidate.process(input, { 1'000.0f, 0.85f, 12.0f });
		const auto output16 = reference16.process(input, { 1'000.0, 0.85, 12.0 });
		const auto output32 = reference32.process(input, { 1'000.0, 0.85, 12.0 });
		if (sample >= settle)
		{
			candidateSamples[static_cast<std::size_t>(sample - settle)] = candidateOutput;
			samples16[static_cast<std::size_t>(sample - settle)] = output16;
			samples32[static_cast<std::size_t>(sample - settle)] = output32;
		}
	}
	const auto measured = vekt::audio_analysis::measureTwoToneComponents<float>(
		candidateSamples, rate, firstHz, secondHz, settle);
	const auto sixteen = vekt::audio_analysis::measureTwoToneComponents<double>(
		samples16, rate, firstHz, secondHz, settle);
	const auto thirtyTwo = vekt::audio_analysis::measureTwoToneComponents<double>(
		samples32, rate, firstHz, secondHz, settle);
	CAPTURE(measured.firstFundamental, measured.secondFundamental,
		measured.lowerThirdOrder, measured.upperThirdOrder,
		thirtyTwo.firstFundamental, thirtyTwo.secondFundamental,
		thirtyTwo.lowerThirdOrder, thirtyTwo.upperThirdOrder);
	REQUIRE(candidate.diagnostics().unconvergedSamples == 0);
	REQUIRE(candidate.diagnostics().nonFiniteSamples == 0);
	for (const auto* reference : { &reference16, &reference32 })
	{
		REQUIRE(reference->diagnostics().unconvergedSteps == 0);
		REQUIRE(reference->diagnostics().nonFiniteSteps == 0);
		REQUIRE(reference->diagnostics().maximumResidual <= 1.0e-13);
	}
	const auto gainErrorDb = [](double candidatePeak, double referencePeak)
	{
		return std::abs(vekt::audio_analysis::gainToDecibels(candidatePeak / referencePeak));
	};
	REQUIRE(gainErrorDb(sixteen.firstFundamental, thirtyTwo.firstFundamental) < 0.001);
	REQUIRE(gainErrorDb(sixteen.secondFundamental, thirtyTwo.secondFundamental) < 0.001);
	REQUIRE(std::abs(sixteen.lowerThirdOrder - thirtyTwo.lowerThirdOrder) < 1.0e-5);
	REQUIRE(std::abs(sixteen.upperThirdOrder - thirtyTwo.upperThirdOrder) < 1.0e-5);
	REQUIRE(gainErrorDb(measured.firstFundamental, thirtyTwo.firstFundamental) < 0.03);
	REQUIRE(gainErrorDb(measured.secondFundamental, thirtyTwo.secondFundamental) < 0.03);
	REQUIRE(std::abs(measured.lowerThirdOrder - thirtyTwo.lowerThirdOrder) < 1.0e-3);
	REQUIRE(std::abs(measured.upperThirdOrder - thirtyTwo.upperThirdOrder) < 1.0e-3);
}

TEST_CASE("Nonlinear TPT ladder deep stopband is judged by absolute output error", "[audio-lab][kobber][ladder-stopband]")
{
	for (const auto rate : { 44'100.0, 48'000.0, 88'200.0, 96'000.0, 192'000.0 })
		for (const auto frequency : { 4'000.0, 8'000.0, 12'000.0 })
		{
			vekt::audio_lab::NonlinearTptLadder ladder;
			ladder.prepare(rate);
			const auto settle = static_cast<int>(rate * 0.25);
			const auto measurement = static_cast<int>(rate * 0.5);
			vekt::audio_analysis::SinusoidalProjector input(rate, frequency,
				static_cast<std::size_t>(settle));
			vekt::audio_analysis::SinusoidalProjector output(rate, frequency,
				static_cast<std::size_t>(settle));
			for (int sample = 0; sample < settle + measurement; ++sample)
			{
				const auto excitation = 0.001f * static_cast<float>(std::sin(
					2.0 * std::numbers::pi * frequency * sample / rate));
				const auto value = ladder.process(excitation, { 500.0f, 0.0f, 0.0f });
				if (sample >= settle)
				{
					input.add(excitation);
					output.add(value);
				}
			}
			const auto analytical = std::abs(vekt::audio_lab::nonlinearTptLadderDiscreteResponse(
				rate, frequency, { 500.0, 0.0, 0.0 }));
			const auto measured = output.peakAmplitude() / input.peakAmplitude();
			const auto absoluteError = 0.001 * std::abs(measured - analytical);
			CAPTURE(rate, frequency, analytical, measured, absoluteError);
			REQUIRE(std::isfinite(measured));
			REQUIRE(0.001 * analytical < 1.0e-6);
			REQUIRE(absoluteError <= 1.0e-7);
			REQUIRE(ladder.diagnostics().unconvergedSamples == 0);
			REQUIRE(ladder.diagnostics().nonFiniteSamples == 0);
		}
}

TEST_CASE("Ladder prototype writes readable outputs", "[audio-lab][kobber][ladder-candidate][output][slow]")
{
	const auto result = vekt::audio_lab::renderLadderPrototype(24'000.0, 97);
	juce::TemporaryFile wav(".wav"), json(".json");
	REQUIRE(vekt::audio_lab::writeLadderPrototypeWav(wav.getFile(), result, 24'000.0));
	REQUIRE(vekt::audio_lab::writeLadderPrototypeReport(json.getFile(), result));
	REQUIRE(juce::JSON::parse(json.getFile()).getProperty("model", {}).toString()
		== "four-stage-nonlinear-tpt-bounded-newton");
}