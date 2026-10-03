#include <NonlinearTptLadder.h>
#include "KobberOnsetAnalysis.h"
#include "KobberLadderStability.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <vector>

namespace
{
struct Tail
{
	double rms {}, frequency {}, drift {};
	bool valid {};
};

vekt::audio_lab::OnsetFit weakFit(double rate, float cutoff, float resonance,
	bool& converged)
{
	vekt::kobber::NonlinearTptLadder ladder;
	ladder.prepare(rate);
	const vekt::kobber::NonlinearTptLadderSettings settings { cutoff, resonance, 0.0f };
	const auto binLength = static_cast<int>(std::ceil(rate * std::max(0.02, 4.0 / cutoff)));
	const auto exciteLength = static_cast<int>(std::ceil(rate * std::max(0.1, 8.0 / cutoff)));
	const auto excitationHz = std::min(317.0, cutoff * 0.73);
	std::vector<double> bins;
	bins.reserve(15);
	double squares {};
	for (int sample = 0; sample < exciteLength + binLength * 15; ++sample)
	{
		const auto input = sample < exciteLength ? 1.0e-4f * static_cast<float>(
			std::sin(2.0 * std::numbers::pi * excitationHz * sample / rate)) : 0.0f;
		const auto value = ladder.processCoupled(input, settings);
		if (!std::isfinite(value)) { converged = false; return {}; }
		if (sample < exciteLength) continue;
		squares += static_cast<double>(value) * value;
		if ((sample - exciteLength + 1) % binLength == 0)
		{
			bins.push_back(std::sqrt(squares / binLength));
			squares = 0.0;
		}
	}
	converged &= ladder.diagnostics().unconvergedSamples == 0
		&& ladder.diagnostics().nonFiniteSamples == 0;
	return vekt::audio_lab::fitOnset(bins, binLength / rate);
}

Tail fullResonance(double rate, float cutoff, bool& converged)
{
	vekt::kobber::NonlinearTptLadder ladder;
	ladder.prepare(rate);
	const vekt::kobber::NonlinearTptLadderSettings settings { cutoff, 1.0f, 0.0f };
	const auto exciteLength = static_cast<int>(std::ceil(rate * 0.5));
	const auto window = static_cast<int>(std::ceil(rate * std::max(0.2, 100.0 / cutoff)));
	const auto tailLength = static_cast<int>(std::ceil(rate * 3.0));
	const auto excitationHz = std::min(317.0, cutoff * 0.73);
	double earlierSquares {}, lateSquares {};
	int crossings {}, first = -1, last = -1;
	float previous {};
	for (int sample = 0; sample < exciteLength + tailLength; ++sample)
	{
		const auto input = sample < exciteLength ? 0.5f * static_cast<float>(
			std::sin(2.0 * std::numbers::pi * excitationHz * sample / rate)) : 0.0f;
		const auto value = ladder.processCoupled(input, settings);
		if (!std::isfinite(value)) { converged = false; return {}; }
		if (sample >= exciteLength + tailLength - 2 * window
			&& sample < exciteLength + tailLength - window)
			earlierSquares += static_cast<double>(value) * value;
		if (sample >= exciteLength + tailLength - window)
		{
			lateSquares += static_cast<double>(value) * value;
			if (previous <= 0.0f && value > 0.0f)
			{
				if (first < 0) first = sample;
				last = sample;
				++crossings;
			}
			previous = value;
		}
	}
	converged &= ladder.diagnostics().unconvergedSamples == 0
		&& ladder.diagnostics().nonFiniteSamples == 0;
	const auto earlier = std::sqrt(earlierSquares / window);
	const auto late = std::sqrt(lateSquares / window);
	Tail result;
	result.rms = late;
	if (crossings > 1 && earlier > 1.0e-5 && late > 1.0e-5)
	{
		result.frequency = (crossings - 1) * rate / (last - first);
		result.drift = 20.0 * std::log10(late / earlier) / (window / rate);
		result.valid = true;
	}
	return result;
}
}

int main(int argc, char** argv)
{
	if (argc != 2)
	{
		std::cerr << "Usage: VektKobberResonanceMatrix output.csv\n";
		return 64;
	}
	std::ofstream output(argv[1]);
	if (!output) return 1;
	output << std::setprecision(12)
		<< "sample_rate_hz,cutoff_hz,onset_lower_resonance,onset_upper_resonance,"
		<< "onset_lower_lambda_per_second,onset_upper_lambda_per_second,"
		<< "valid_weak_fits,raw_ladder_rms_at_100,oscillation_frequency_hz,"
		<< "frequency_error_cents,late_rms_drift_db_per_second,solver_converged,"
		<< "linear_onset_lower_resonance,linear_onset_upper_resonance,"
		<< "linear_radius_below,linear_radius_above,linear_onset_frequency_hz,"
		<< "linear_jacobian_error_h1,linear_jacobian_error_h2,linear_jacobian_error_h3\n";
	for (const auto rate : { 44'100.0, 48'000.0, 96'000.0, 192'000.0 })
		for (const auto cutoff : { 100.0f, 250.0f, 500.0f, 1'000.0f, 2'000.0f, 5'000.0f, 10'000.0f })
		{
			bool converged = true;
			int validFits {};
			bool bracketed {};
			float lower {}, upper {};
			double lowerSlope {}, upperSlope {};
			// Search in ascending resonance order. Never bridge an invalid fit.
			bool previousValid {};
			float previousResonance {};
			double previousSlope {};
			for (int step = 0; step <= 30; ++step)
			{
				const auto resonance = static_cast<float>(0.94 + step * 0.002);
				const auto fit = weakFit(rate, cutoff, resonance, converged);
				validFits += fit.valid;
				if (!bracketed && previousValid && fit.valid && previousSlope < 0.0
					&& fit.slopePerSecond > 0.0)
				{
					lower = previousResonance; upper = resonance;
					lowerSlope = previousSlope; upperSlope = fit.slopePerSecond;
					bracketed = true;
				}
				previousValid = fit.valid;
				previousResonance = resonance;
				previousSlope = fit.slopePerSecond;
			}
			// Refine only when both endpoint fits remain trustworthy.
			for (int iteration = 0; bracketed && iteration < 5 && upper - lower > 0.0001f; ++iteration)
			{
				const auto middle = static_cast<float>((lower + upper) * 0.5);
				const auto fit = weakFit(rate, cutoff, middle, converged);
				validFits += fit.valid;
				if (!fit.valid) break;
				if (fit.slopePerSecond < 0.0) { lower = middle; lowerSlope = fit.slopePerSecond; }
				else if (fit.slopePerSecond > 0.0) { upper = middle; upperSlope = fit.slopePerSecond; }
				else break;
			}
			const auto tail = fullResonance(rate, cutoff, converged);
			// Bisection on the analytic derivative of the exact zero-input
			// coupled equations; report the bracket, not an interpolated knob value.
			float linearLower = 0.94f, linearUpper = 1.0f;
			auto below = vekt::audio_lab::ladderStability(rate, cutoff, linearLower);
			auto above = vekt::audio_lab::ladderStability(rate, cutoff, linearUpper);
			if (!(below.radius < 1.0 && above.radius > 1.0))
			{
				std::cerr << "Unbracketed linear onset at " << rate << " / " << cutoff << '\n';
				return 1;
			}
			for (int iteration = 0; iteration < 16; ++iteration)
			{
				const auto middle = static_cast<float>((linearLower + linearUpper) * 0.5f);
				const auto measured = vekt::audio_lab::ladderStability(rate, cutoff, middle);
				if (measured.radius < 1.0) { linearLower = middle; below = measured; }
				else { linearUpper = middle; above = measured; }
			}
			std::array<double, 3> jacobianErrors {};
			for (std::size_t i = 0; i < jacobianErrors.size(); ++i)
			{
				const auto step = 0.01 * std::pow(0.1, i);
				const auto measured = vekt::audio_lab::measuredLadderJacobian(rate, cutoff,
					static_cast<float>((linearLower + linearUpper) * 0.5f), step, converged);
				jacobianErrors[i] = vekt::audio_lab::maximumJacobianError(measured,
					vekt::audio_lab::ladderStability(rate, cutoff,
						static_cast<float>((linearLower + linearUpper) * 0.5f)).jacobian);
			}
			if (!converged || jacobianErrors[0] > 1.0e-3
				|| jacobianErrors[1] > 1.0e-7 || jacobianErrors[2] > 1.0e-7)
			{
				std::cerr << "Production Jacobian mismatch at " << rate << " / " << cutoff << '\n';
				return 1;
			}
			output << rate << ',' << cutoff << ',';
			if (bracketed) output << lower << ',' << upper << ',' << lowerSlope << ',' << upperSlope;
			else output << ",,,";
			output << ',' << validFits << ',' << tail.rms << ',';
			if (tail.valid)
				output << tail.frequency << ',' << 1200.0 * std::log2(tail.frequency / cutoff)
					<< ',' << tail.drift;
			else output << ",,";
			output << ',' << converged << ',' << linearLower << ',' << linearUpper
				<< ',' << below.radius << ',' << above.radius
				<< ',' << above.frequencyHz;
			for (const auto error : jacobianErrors) output << ',' << error;
			output << '\n';
			std::cout << "rate=" << rate << " cutoff=" << cutoff << " bracket=" << bracketed
				<< " rms=" << tail.rms << " converged=" << converged << std::endl;
			if (!converged || !output) return 1;
		}
	return 0;
}