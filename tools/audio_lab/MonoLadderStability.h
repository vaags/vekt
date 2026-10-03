#pragma once

#include <LadderResonance.h>
#include <NonlinearTptLadder.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <complex>
#include <numbers>

namespace vekt::audio_lab
{
using LadderMatrix = std::array<std::array<double, 4>, 4>;

// At zero input tanh'(0)=1. The exact coupled solve has
// ((1+g)I-gC)y=s, C(y)=[-k*y3,y0,y1,y2]. The production state
// transition is s'=2y-s. Since C^4=-kI, its four modes are explicit.
struct LadderStability
{
	double radius {}, frequencyHz {}, dominantGrowthPerSecond {};
	LadderMatrix jacobian {};
};

[[nodiscard]] inline LadderStability ladderStability(double rate, float cutoff,
	float resonance)
{
	const auto clamped = std::clamp(cutoff, 10.0f, static_cast<float>(rate) * 0.45f);
	const auto g = std::tan(std::numbers::pi_v<double> * clamped / static_cast<float>(rate))
		* mono::ladderResonanceTuning(static_cast<double>(resonance));
	const auto k = mono::ladderFeedbackGain(static_cast<double>(resonance));
	const auto a = g / (1.0 + g);
	const auto denominator = (1.0 + g) * (1.0 + k * a * a * a * a);
	LadderStability result;
	LadderMatrix power {};
	for (std::size_t i = 0; i < 4; ++i) power[i][i] = 1.0;
	for (int exponent = 0; exponent < 4; ++exponent)
	{
		const auto scale = 2.0 * std::pow(a, exponent) / denominator;
		for (std::size_t i = 0; i < 4; ++i)
			for (std::size_t j = 0; j < 4; ++j)
				result.jacobian[i][j] += scale * power[i][j];
		LadderMatrix next {};
		for (std::size_t j = 0; j < 4; ++j)
		{
			next[0][j] = -k * power[3][j];
			for (std::size_t i = 1; i < 4; ++i) next[i][j] = power[i - 1][j];
		}
		power = next;
	}
	for (std::size_t i = 0; i < 4; ++i) result.jacobian[i][i] -= 1.0;
	const auto root = std::pow(k, 0.25);
	for (int mode = 0; mode < 4; ++mode)
	{
		const auto c = std::polar(root, std::numbers::pi_v<double> * (2 * mode + 1) / 4.0);
		const auto eigenvalue = 2.0 / (1.0 + g - g * c) - 1.0;
		if (std::abs(eigenvalue) > result.radius)
		{
			result.radius = std::abs(eigenvalue);
			result.frequencyHz = std::abs(std::arg(eigenvalue)) * rate / (2.0 * std::numbers::pi_v<double>);
		}
	}
	result.dominantGrowthPerSecond = rate * std::log(result.radius);
	return result;
}

// Central differences of the actual bounded-Newton production state step.
[[nodiscard]] inline LadderMatrix measuredLadderJacobian(double rate, float cutoff,
	float resonance, double step, bool& converged)
{
	LadderMatrix result {};
	for (std::size_t column = 0; column < 4; ++column)
	{
		std::array<double, 4> states[2];
		for (std::size_t sign = 0; sign < 2; ++sign)
		{
			mono::NonlinearTptLadder ladder;
			ladder.prepare(rate);
			std::array<double, 4> state {};
			state[column] = sign ? step : -step;
			ladder.setAnalysisIntegratorState(state);
			const auto sample = ladder.processCoupled(0.0f, { cutoff, resonance, 0.0f });
			converged &= std::isfinite(sample) && ladder.diagnostics().unconvergedSamples == 0
				&& ladder.diagnostics().nonFiniteSamples == 0;
			states[sign] = ladder.analysisIntegratorState();
		}
		for (std::size_t row = 0; row < 4; ++row)
			result[row][column] = (states[1][row] - states[0][row]) / (2.0 * step);
	}
	return result;
}

[[nodiscard]] inline double maximumJacobianError(const LadderMatrix& a, const LadderMatrix& b)
{
	double error {};
	for (std::size_t i = 0; i < 4; ++i)
		for (std::size_t j = 0; j < 4; ++j) error = std::max(error, std::abs(a[i][j] - b[i][j]));
	return error;
}
}