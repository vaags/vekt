#include "NonlinearTptLadder.h"
#include "LadderResonance.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace vekt::mono
{
namespace
{
constexpr int maximumFeedbackIterations = 16;
constexpr int maximumStageIterations = 24;
constexpr double convergenceTolerance = 2.0e-7;
constexpr double signalLimit = 24.0;

[[nodiscard]] double sechSquared(double value) noexcept
{
	const auto tangent = std::tanh(value);
	return std::max(0.0, 1.0 - tangent * tangent);
}
}

void NonlinearTptLadder::prepare(double newSampleRate) noexcept
{
	sampleRate = static_cast<float>(std::max(1.0, newSampleRate));
	reset();
}

void NonlinearTptLadder::reset() noexcept
{
	previousFeedbackInput = 0.0;
	integratorState = {};
	previousOutput = {};
	solverDiagnostics = {};
	previousInput = 0.0f;
	previousSettings = {};
	hasPreviousSettings = false;
}

NonlinearTptLadder::Evaluation NonlinearTptLadder::evaluate(double input, double integrationGain) const noexcept
{
	Evaluation evaluation;
	auto stageInput = input;
	auto derivative = 1.0;
	for (std::size_t stage = 0; stage < evaluation.output.size(); ++stage)
	{
		auto output = std::clamp(integratorState[stage]
			+ integrationGain * (std::tanh(stageInput) - std::tanh(integratorState[stage])),
			-signalLimit, signalLimit);
		// The monotone stage residual has its root within state +/- 2g.
		auto lower = integratorState[stage] - 2.0 * integrationGain;
		auto upper = integratorState[stage] + 2.0 * integrationGain;
		bool converged = false;
		int iteration = 0;
		double residual = 0.0;
		for (; iteration < maximumStageIterations; ++iteration)
		{
			residual = output - integratorState[stage]
				- integrationGain * (std::tanh(stageInput) - std::tanh(output));
			if (std::abs(residual) <= convergenceTolerance)
			{
				converged = true;
				break;
			}
			if (residual > 0.0) upper = output;
			else lower = output;
			const auto slope = 1.0 + integrationGain * sechSquared(output);
			const auto next = output - residual / slope;
			const auto width = upper - lower;
			output = next > lower && next < upper && std::abs(next - output) <= 0.5 * width
				? next : lower + 0.5 * width;
		}
		residual = output - integratorState[stage]
			- integrationGain * (std::tanh(stageInput) - std::tanh(output));
		converged = converged || std::abs(residual) <= convergenceTolerance;
		evaluation.maximumResidual = std::max(evaluation.maximumResidual, std::abs(residual));
		evaluation.maximumIterations = std::max(evaluation.maximumIterations, iteration + (converged ? 1 : 0));
		evaluation.converged = evaluation.converged && converged;
		derivative *= integrationGain * sechSquared(stageInput)
			/ (1.0 + integrationGain * sechSquared(output));
		evaluation.output[stage] = output;
		stageInput = output;
	}
	evaluation.derivative = derivative;
	return evaluation;
}

float NonlinearTptLadder::process(float input, const NonlinearTptLadderSettings& settings) noexcept
{
	const auto maximumCutoff = sampleRate * 0.45f;
	const auto cutoff = std::clamp(settings.cutoffHz, 10.0f, maximumCutoff);
	const auto integrationGain = std::tan(std::numbers::pi_v<double> * cutoff / sampleRate)
		* ladderResonanceTuning(static_cast<double>(settings.resonance));
	return processStep(input, settings, integrationGain);
}

float NonlinearTptLadder::processSubstepped(float input,
	const NonlinearTptLadderSettings& settings, int substeps) noexcept
{
	// The cap bounds work independently of an accidental caller-supplied count.
	const auto count = std::clamp(substeps, 1, 4);
	if (!hasPreviousSettings)
	{
		previousSettings = settings;
		hasPreviousSettings = true;
	}
	float output {};
	for (int step = 1; step <= count; ++step)
	{
		const auto fraction = static_cast<double>(step) / count;
		const auto interpolate = [fraction](float previous, float current)
		{
			return static_cast<float>(static_cast<double>(previous)
				+ fraction * (static_cast<double>(current) - previous));
		};
		const auto stepInput = interpolate(previousInput, input);
		const auto cutoff = std::clamp(interpolate(previousSettings.cutoffHz, settings.cutoffHz),
			10.0f, sampleRate * 0.45f);
		const auto integrationGain = std::tan(std::numbers::pi_v<double> * cutoff / sampleRate)
			* ladderResonanceTuning(static_cast<double>(interpolate(previousSettings.resonance, settings.resonance))) / count;
		const NonlinearTptLadderSettings stepSettings {
			cutoff,
			interpolate(previousSettings.resonance, settings.resonance),
			interpolate(previousSettings.driveDecibels, settings.driveDecibels),
			settings.driveCompensation
		};
		output = processStep(stepInput, stepSettings, integrationGain);
	}
	previousInput = input;
	previousSettings = settings;
	return output;
}

float NonlinearTptLadder::processStep(float input,
	const NonlinearTptLadderSettings& settings, double integrationGain) noexcept
{
	const auto resonance = std::clamp(settings.resonance, 0.0f, 1.0f);
	const auto feedbackGain = ladderFeedbackGain(static_cast<double>(resonance));
	const auto driveGain = std::pow(10.0f, settings.driveDecibels / 20.0f);
	const auto drivenInput = std::clamp(static_cast<double>(input) * driveGain,
		-signalLimit, signalLimit);
	// |y4| <= |state4| + 2g, so this bracket contains the feedback root.
	const auto feedbackSpan = feedbackGain * (signalLimit + 2.0 * integrationGain);
	auto lower = drivenInput - feedbackSpan;
	auto upper = drivenInput + feedbackSpan;
	auto feedbackInput = std::clamp(previousFeedbackInput, lower, upper);
	Evaluation evaluation;
	bool converged = false;
	int iteration = 0;
	double residual = 0.0;
	for (; iteration < maximumFeedbackIterations; ++iteration)
	{
		evaluation = evaluate(feedbackInput, integrationGain);
		residual = (feedbackInput - drivenInput) + feedbackGain * evaluation.output.back();
		if (std::abs(residual) <= convergenceTolerance)
		{
			converged = evaluation.converged;
			break;
		}
		if (residual > 0.0) upper = feedbackInput;
		else lower = feedbackInput;
		const auto slope = 1.0 + feedbackGain * evaluation.derivative;
		const auto next = feedbackInput - residual / slope;
		const auto width = upper - lower;
		feedbackInput = next > lower && next < upper
			&& std::abs(next - feedbackInput) <= 0.5 * width
			? next : lower + 0.5 * width;
	}
	if (!converged)
	{
		evaluation = evaluate(feedbackInput, integrationGain);
		residual = (feedbackInput - drivenInput) + feedbackGain * evaluation.output.back();
		converged = evaluation.converged && std::abs(residual) <= convergenceTolerance;
	}

	++solverDiagnostics.samples;
	solverDiagnostics.maximumFeedbackIterations = std::max(solverDiagnostics.maximumFeedbackIterations,
		iteration + (converged ? 1 : 0));
	solverDiagnostics.maximumStageIterations = std::max(solverDiagnostics.maximumStageIterations,
		evaluation.maximumIterations);
	solverDiagnostics.maximumResidual = std::max(solverDiagnostics.maximumResidual,
		static_cast<float>(std::max(std::abs(residual), evaluation.maximumResidual)));
	if (!converged) ++solverDiagnostics.unconvergedSamples;

	const auto output = evaluation.output.back();
	if (!std::isfinite(output))
	{
		++solverDiagnostics.nonFiniteSamples;
		integratorState = {};
		previousOutput = {};
		previousFeedbackInput = 0.0;
		return 0.0f;
	}
	for (std::size_t stage = 0; stage < integratorState.size(); ++stage)
	{
		integratorState[stage] = std::clamp(2.0 * evaluation.output[stage] - integratorState[stage],
			-signalLimit, signalLimit);
		previousOutput[stage] = evaluation.output[stage];
	}
	previousFeedbackInput = feedbackInput;
	return static_cast<float>(settings.driveCompensation ? output / std::sqrt(driveGain) : output);
}

float NonlinearTptLadder::processCoupled(float input, const NonlinearTptLadderSettings& settings) noexcept
{
	constexpr int maximumCoupledIterations = 16;
	constexpr int maximumLineSearchSteps = 10;
	const auto cutoff = std::clamp(settings.cutoffHz, 10.0f, sampleRate * 0.45f);
	const auto g = std::tan(std::numbers::pi_v<double> * cutoff / sampleRate)
		* ladderResonanceTuning(static_cast<double>(settings.resonance));
	const auto k = ladderFeedbackGain(static_cast<double>(settings.resonance));
	const auto driveGain = std::pow(10.0f, settings.driveDecibels / 20.0f);
	const auto driven = std::clamp(static_cast<double>(input) * driveGain, -signalLimit, signalLimit);
	// Feed a bounded fraction of the driven input around the global feedback.
	// With zero input this is exactly the uncompensated feedback system.
	const auto excitation = driven + k * std::clamp(static_cast<double>(settings.inputFeedbackCompensation), 0.0, 0.5) * driven;
	std::array<double, 4> output = previousOutput;
	const auto residuals = [&](const std::array<double, 4>& values)
	{
		std::array<double, 4> residual {};
		auto stageInput = excitation - k * values[3];
		for (std::size_t stage = 0; stage < values.size(); ++stage)
		{
			residual[stage] = values[stage] - integratorState[stage]
				- g * (std::tanh(stageInput) - std::tanh(values[stage]));
			stageInput = values[stage];
		}
		return residual;
	};
	const auto norm = [](const std::array<double, 4>& residual)
	{
		double maximum {};
		for (const auto value : residual)
		{
			if (!std::isfinite(value)) return std::numeric_limits<double>::infinity();
			maximum = std::max(maximum, std::abs(value));
		}
		return maximum;
	};
	auto residual = residuals(output);
	auto error = norm(residual);
	int iterations {};
	std::uint64_t lineSearchTrials {};
	for (; iterations < maximumCoupledIterations && error > convergenceTolerance; ++iterations)
	{
		// Forward substitution with an affine dependence on delta[3] solves
		// the cyclic 4x4 Jacobian without a generic matrix factorization.
		std::array<double, 4> independent {}, dependent {};
		const auto u = excitation - k * output[3];
		const auto diagonal = 1.0 + g * sechSquared(output[0]);
		independent[0] = -residual[0] / diagonal;
		dependent[0] = -g * k * sechSquared(u) / diagonal;
		for (std::size_t stage = 1; stage < output.size(); ++stage)
		{
			const auto coupling = g * sechSquared(output[stage - 1]);
			const auto scale = 1.0 / (1.0 + g * sechSquared(output[stage]));
			independent[stage] = (-residual[stage] + coupling * independent[stage - 1]) * scale;
			dependent[stage] = coupling * dependent[stage - 1] * scale;
		}
		const auto last = independent[3] / (1.0 - dependent[3]);
		std::array<double, 4> step {};
		for (std::size_t stage = 0; stage < step.size(); ++stage)
			step[stage] = independent[stage] + dependent[stage] * last;
		bool accepted = false;
		for (int attempt = 0; attempt < maximumLineSearchSteps; ++attempt)
		{
			++lineSearchTrials;
			const auto damping = std::ldexp(1.0, -attempt);
			auto trial = output;
			for (std::size_t stage = 0; stage < trial.size(); ++stage)
				trial[stage] += damping * step[stage];
			const auto trialResidual = residuals(trial);
			const auto trialError = norm(trialResidual);
			if (std::isfinite(trialError) && trialError < error)
			{
				output = trial;
				residual = trialResidual;
				error = trialError;
				accepted = true;
				break;
			}
		}
		if (!accepted) break;
	}
	++solverDiagnostics.samples;
	solverDiagnostics.coupledIterations += static_cast<std::uint64_t>(iterations);
	solverDiagnostics.coupledLineSearchTrials += lineSearchTrials;
	solverDiagnostics.maximumFeedbackIterations = std::max(solverDiagnostics.maximumFeedbackIterations, iterations);
	solverDiagnostics.maximumResidual = std::max(solverDiagnostics.maximumResidual, static_cast<float>(error));
	if (error > convergenceTolerance) ++solverDiagnostics.unconvergedSamples;
	if (!std::isfinite(error)
		|| !std::all_of(output.begin(), output.end(), [](double value) { return std::isfinite(value); }))
	{
		++solverDiagnostics.nonFiniteSamples;
		integratorState = {};
		previousOutput = {};
		previousFeedbackInput = 0.0;
		return 0.0f;
	}
	for (std::size_t stage = 0; stage < output.size(); ++stage)
	{
		integratorState[stage] = std::clamp(2.0 * output[stage] - integratorState[stage],
			-signalLimit, signalLimit);
		previousOutput[stage] = output[stage];
	}
	previousFeedbackInput = excitation - k * output[3];
	return static_cast<float>(settings.driveCompensation ? output[3] / std::sqrt(driveGain) : output[3]);
}
}