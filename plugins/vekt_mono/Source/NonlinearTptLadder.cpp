#include "NonlinearTptLadder.h"
#include "LadderResonance.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <numbers>

#if defined(__APPLE__)
#include <simd/simd.h>
#endif

namespace vekt::mono
{
namespace
{
constexpr int maximumFeedbackIterations = 16;
constexpr int maximumStageIterations = 24;
constexpr double convergenceTolerance = 2.0e-7;
constexpr double signalLimit = 24.0;

[[nodiscard]] double sechSquaredFromTanh(double tangent) noexcept
{
	return std::max(0.0, 1.0 - tangent * tangent);
}

template <std::size_t Lanes>
using LaneValues = std::array<double, Lanes>;

// tanh of every lane at once. Apple's vector tanh is about 2.4x faster than scalar libm and within ~2 ulp of it.
template <std::size_t Lanes>
[[nodiscard]] LaneValues<Lanes> tanhLanes(const LaneValues<Lanes>& values) noexcept
{
#if defined(__APPLE__)
	if constexpr (Lanes == 2)
	{
		const auto result = simd::tanh(simd_double2 { values[0], values[1] });
		return { result[0], result[1] };
	}
	else if constexpr (Lanes == 4)
	{
		const auto result = simd::tanh(simd_double4 { values[0], values[1], values[2], values[3] });
		return { result[0], result[1], result[2], result[3] };
	}
#endif
	LaneValues<Lanes> result {};
	for (std::size_t lane = 0; lane < Lanes; ++lane) result[lane] = std::tanh(values[lane]);
	return result;
}

[[nodiscard]] double sechSquared(double value) noexcept
{
	return sechSquaredFromTanh(std::tanh(value));
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

void NonlinearTptLadder::setAnalysisIntegratorState(const std::array<double, 4>& state) noexcept
{
	reset();
	integratorState = state;
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
	const auto driveGain = driveGainFor(settings.driveDecibels);
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
	const auto driveGain = driveGainFor(settings.driveDecibels);
	const auto driven = std::clamp(static_cast<double>(input) * driveGain, -signalLimit, signalLimit);
	// Feed a bounded fraction of the driven input around the global feedback.
	// With zero input this is exactly the uncompensated feedback system.
	const auto excitation = driven + k * std::clamp(static_cast<double>(settings.inputFeedbackCompensation), 0.0, 0.5) * driven;
	std::array<double, 4> output = previousOutput;
	// tanh dominates the cost. Each distinct argument is evaluated once per trial point: stage s's input is
	// stage s-1's output, and the Newton step reuses the tanh values from the residual at the current point.
	// This is exactly the arithmetic of evaluating them separately, so output is bit-identical.
	struct Point
	{
		std::array<double, 4> residual {}, stageTanh {};
		double inputTanh {};
	};
	const auto evaluatePoint = [&](const std::array<double, 4>& values)
	{
		Point point;
		point.inputTanh = std::tanh(excitation - k * values[3]);
		auto inputTanh = point.inputTanh;
		for (std::size_t stage = 0; stage < values.size(); ++stage)
		{
			point.stageTanh[stage] = std::tanh(values[stage]);
			point.residual[stage] = values[stage] - integratorState[stage] - g * (inputTanh - point.stageTanh[stage]);
			inputTanh = point.stageTanh[stage];
		}
		return point;
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
	auto point = evaluatePoint(output);
	auto error = norm(point.residual);
	int iterations {};
	std::uint64_t lineSearchTrials {};
	for (; iterations < maximumCoupledIterations && error > convergenceTolerance; ++iterations)
	{
		// Forward substitution with an affine dependence on delta[3] solves
		// the cyclic 4x4 Jacobian without a generic matrix factorization.
		std::array<double, 4> independent {}, dependent {};
		const auto diagonal = 1.0 + g * sechSquaredFromTanh(point.stageTanh[0]);
		independent[0] = -point.residual[0] / diagonal;
		dependent[0] = -g * k * sechSquaredFromTanh(point.inputTanh) / diagonal;
		for (std::size_t stage = 1; stage < output.size(); ++stage)
		{
			const auto coupling = g * sechSquaredFromTanh(point.stageTanh[stage - 1]);
			const auto scale = 1.0 / (1.0 + g * sechSquaredFromTanh(point.stageTanh[stage]));
			independent[stage] = (-point.residual[stage] + coupling * independent[stage - 1]) * scale;
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
			const auto trialPoint = evaluatePoint(trial);
			const auto trialError = norm(trialPoint.residual);
			if (std::isfinite(trialError) && trialError < error)
			{
				output = trial;
				point = trialPoint;
				error = trialError;
				accepted = true;
				break;
			}
		}
		if (!accepted) break;
	}
	return completeCoupledStep(output, error, iterations, lineSearchTrials, excitation - k * output[3], driveGain,
		settings.driveCompensation);
}

float NonlinearTptLadder::completeCoupledStep(const std::array<double, 4>& output, double error, int iterations,
	std::uint64_t lineSearchTrials, double feedbackInput, float driveGain, bool driveCompensation) noexcept
{
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
	previousFeedbackInput = feedbackInput;
	return static_cast<float>(driveCompensation ? output[3] / std::sqrt(driveGain) : output[3]);
}
void NonlinearTptLadder::processCoupled(std::span<NonlinearTptLadder> ladders, std::span<const float> inputs,
	std::span<float> outputs, const NonlinearTptLadderSettings& settings) noexcept
{
	// The batched solve shares one integration gain, which depends on the sample rate; ladders prepared at
	// different rates are solved one by one instead. The rates must be bit-identical (not merely close) for the
	// shared gain to equal each lane's own, so compare their bits.
	const auto sameRate = std::all_of(ladders.begin(), ladders.end(), [&](const NonlinearTptLadder& ladder)
	{
		return std::bit_cast<std::uint32_t>(ladder.sampleRate) == std::bit_cast<std::uint32_t>(ladders.front().sampleRate);
	});
	switch (sameRate ? ladders.size() : 0)
	{
	case 2: processCoupledLanes<2>(ladders, inputs, outputs, settings); return;
	case 4: processCoupledLanes<4>(ladders, inputs, outputs, settings); return;
	default:
		for (std::size_t lane = 0; lane < ladders.size(); ++lane) outputs[lane] = ladders[lane].processCoupled(inputs[lane], settings);
	}
}

template <std::size_t Lanes>
void NonlinearTptLadder::processCoupledLanes(std::span<NonlinearTptLadder> ladders, std::span<const float> inputs,
	std::span<float> outputs, const NonlinearTptLadderSettings& settings) noexcept
{
	// Mirrors processCoupled lane by lane; only tanh is shared across lanes.
	constexpr int maximumCoupledIterations = 16;
	constexpr int maximumLineSearchSteps = 10;
	using Stages = std::array<double, 4>;
	const auto sampleRate = ladders[0].sampleRate;
	const auto cutoff = std::clamp(settings.cutoffHz, 10.0f, sampleRate * 0.45f);
	const auto g = std::tan(std::numbers::pi_v<double> * cutoff / sampleRate)
		* ladderResonanceTuning(static_cast<double>(settings.resonance));
	const auto k = ladderFeedbackGain(static_cast<double>(settings.resonance));
	const auto driveGain = ladders[0].driveGainFor(settings.driveDecibels);
	LaneValues<Lanes> excitation {};
	std::array<Stages, Lanes> output {};
	for (std::size_t lane = 0; lane < Lanes; ++lane)
	{
		const auto driven = std::clamp(static_cast<double>(inputs[lane]) * driveGain, -signalLimit, signalLimit);
		excitation[lane] = driven + k * std::clamp(static_cast<double>(settings.inputFeedbackCompensation), 0.0, 0.5) * driven;
		output[lane] = ladders[lane].previousOutput;
	}
	struct Points
	{
		std::array<Stages, Lanes> residual {}, stageTanh {};
		LaneValues<Lanes> inputTanh {};
	};
	const auto evaluatePoints = [&](const std::array<Stages, Lanes>& values)
	{
		Points points;
		LaneValues<Lanes> arguments {};
		for (std::size_t lane = 0; lane < Lanes; ++lane) arguments[lane] = excitation[lane] - k * values[lane][3];
		points.inputTanh = tanhLanes<Lanes>(arguments);
		for (std::size_t stage = 0; stage < 4; ++stage)
		{
			for (std::size_t lane = 0; lane < Lanes; ++lane) arguments[lane] = values[lane][stage];
			const auto stageTanh = tanhLanes<Lanes>(arguments);
			for (std::size_t lane = 0; lane < Lanes; ++lane) points.stageTanh[lane][stage] = stageTanh[lane];
		}
		for (std::size_t lane = 0; lane < Lanes; ++lane)
		{
			auto inputTanh = points.inputTanh[lane];
			for (std::size_t stage = 0; stage < 4; ++stage)
			{
				points.residual[lane][stage] = values[lane][stage] - ladders[lane].integratorState[stage]
					- g * (inputTanh - points.stageTanh[lane][stage]);
				inputTanh = points.stageTanh[lane][stage];
			}
		}
		return points;
	};
	const auto norm = [](const Stages& residual)
	{
		double maximum {};
		for (const auto value : residual)
		{
			if (!std::isfinite(value)) return std::numeric_limits<double>::infinity();
			maximum = std::max(maximum, std::abs(value));
		}
		return maximum;
	};
	auto points = evaluatePoints(output);
	LaneValues<Lanes> error {};
	std::array<int, Lanes> iterations {};
	std::array<std::uint64_t, Lanes> lineSearchTrials {};
	std::array<bool, Lanes> running {};
	for (std::size_t lane = 0; lane < Lanes; ++lane)
	{
		error[lane] = norm(points.residual[lane]);
		running[lane] = true;
	}
	for (int iteration = 0; iteration < maximumCoupledIterations; ++iteration)
	{
		bool anyRunning = false;
		for (std::size_t lane = 0; lane < Lanes; ++lane)
		{
			running[lane] = running[lane] && error[lane] > convergenceTolerance;
			anyRunning = anyRunning || running[lane];
		}
		if (!anyRunning) break;
		std::array<Stages, Lanes> step {};
		for (std::size_t lane = 0; lane < Lanes; ++lane)
		{
			if (!running[lane]) continue;
			const auto& residual = points.residual[lane];
			const auto& stageTanh = points.stageTanh[lane];
			Stages independent {}, dependent {};
			const auto diagonal = 1.0 + g * sechSquaredFromTanh(stageTanh[0]);
			independent[0] = -residual[0] / diagonal;
			dependent[0] = -g * k * sechSquaredFromTanh(points.inputTanh[lane]) / diagonal;
			for (std::size_t stage = 1; stage < 4; ++stage)
			{
				const auto coupling = g * sechSquaredFromTanh(stageTanh[stage - 1]);
				const auto scale = 1.0 / (1.0 + g * sechSquaredFromTanh(stageTanh[stage]));
				independent[stage] = (-residual[stage] + coupling * independent[stage - 1]) * scale;
				dependent[stage] = coupling * dependent[stage - 1] * scale;
			}
			const auto last = independent[3] / (1.0 - dependent[3]);
			for (std::size_t stage = 0; stage < 4; ++stage)
				step[lane][stage] = independent[stage] + dependent[stage] * last;
		}
		auto searching = running;
		std::array<bool, Lanes> accepted {};
		for (int attempt = 0; attempt < maximumLineSearchSteps; ++attempt)
		{
			if (std::none_of(searching.begin(), searching.end(), [](bool value) { return value; })) break;
			const auto damping = std::ldexp(1.0, -attempt);
			auto trial = output;
			for (std::size_t lane = 0; lane < Lanes; ++lane)
				if (searching[lane])
					for (std::size_t stage = 0; stage < 4; ++stage) trial[lane][stage] += damping * step[lane][stage];
			const auto trialPoints = evaluatePoints(trial);
			for (std::size_t lane = 0; lane < Lanes; ++lane)
			{
				if (!searching[lane]) continue;
				++lineSearchTrials[lane];
				const auto trialError = norm(trialPoints.residual[lane]);
				if (std::isfinite(trialError) && trialError < error[lane])
				{
					output[lane] = trial[lane];
					points.residual[lane] = trialPoints.residual[lane];
					points.stageTanh[lane] = trialPoints.stageTanh[lane];
					points.inputTanh[lane] = trialPoints.inputTanh[lane];
					error[lane] = trialError;
					accepted[lane] = true;
					searching[lane] = false;
				}
			}
		}
		for (std::size_t lane = 0; lane < Lanes; ++lane)
		{
			if (!running[lane]) continue;
			if (accepted[lane]) ++iterations[lane];
			else running[lane] = false;
		}
	}
	for (std::size_t lane = 0; lane < Lanes; ++lane)
		outputs[lane] = ladders[lane].completeCoupledStep(output[lane], error[lane], iterations[lane], lineSearchTrials[lane],
			excitation[lane] - k * output[lane][3], driveGain, settings.driveCompensation);
}
}
