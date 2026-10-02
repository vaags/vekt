#include "NonlinearTptLadder.h"
#include "LadderPoleMix.h"
#include "LadderResonance.h"
#include "SimdLanes.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
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

[[nodiscard]] double sechSquaredFromTanh(double tangent) noexcept
{
	return std::max(0.0, 1.0 - tangent * tangent);
}

template <std::size_t Lanes>
using LaneValues = std::array<double, Lanes>;

[[nodiscard]] double sechSquared(double value) noexcept
{
	return sechSquaredFromTanh(std::tanh(value));
}

// One ladder's coupled solve of the four implicit stage equations, advanced one point evaluation at a time:
// arguments() are the point's five tanh arguments (feedback input, then the four stages), advance() takes their
// tanh. Damped Newton on the max-norm of the residuals, halving the step up to ten times until the norm falls;
// it stops when converged, after 16 accepted steps or when no damped step improves. The scalar solve and the
// batched lanes (tanh shared across lanes) run exactly this logic, so they differ only in how tanh is evaluated.
struct CoupledNewton
{
	static constexpr int maximumIterations = 16;
	static constexpr int maximumLineSearchSteps = 10;
	using Stages = std::array<double, 4>;
	using Arguments = std::array<double, 5>;

	Stages state {}, output {}, trial {}, step {};
	double g {}, k {}, excitation {};
	// At output: the residuals, tanh of each stage and tanh of the feedback input.
	Stages residual {}, stageTanh {};
	double inputTanh {};
	double error {};
	int iterations {}, attempt {};
	std::uint64_t lineSearchTrials {};
	bool started {}, done {};

	void start(const Stages& integratorState, double coefficient, double feedback, double drivenExcitation,
		const Stages& seed) noexcept
	{
		state = integratorState;
		g = coefficient;
		k = feedback;
		excitation = drivenExcitation;
		output = seed;
		trial = seed;
		iterations = 0;
		attempt = 0;
		lineSearchTrials = 0;
		started = false;
		done = false;
	}

	// The point being evaluated: the seed first, then each line-search trial.
	[[nodiscard]] Arguments arguments() const noexcept
	{
		return { excitation - k * trial[3], trial[0], trial[1], trial[2], trial[3] };
	}

	// Forced inline (with accept): outlined, the per-point calls cost the processor about 5 % (Release,
	// VektMonoProcessorCost 48 kHz/257/8 voices/unison 4, 2 October 2026).
	[[gnu::always_inline]] void advance(const Arguments& tanhValues) noexcept
	{
		Stages pointResidual {}, pointTanh {};
		auto stageInput = tanhValues[0];
		for (std::size_t stage = 0; stage < 4; ++stage)
		{
			pointTanh[stage] = tanhValues[stage + 1];
			pointResidual[stage] = trial[stage] - state[stage] - g * (stageInput - pointTanh[stage]);
			stageInput = pointTanh[stage];
		}
		const auto pointError = norm(pointResidual);
		if (!started)
		{
			started = true;
			accept(pointResidual, pointTanh, tanhValues[0], pointError);
			return;
		}
		++lineSearchTrials;
		if (std::isfinite(pointError) && pointError < error)
		{
			output = trial;
			++iterations;
			accept(pointResidual, pointTanh, tanhValues[0], pointError);
			return;
		}
		if (++attempt == maximumLineSearchSteps)
		{
			done = true;
			return;
		}
		damp();
	}

private:
	[[nodiscard]] static double norm(const Stages& values) noexcept
	{
		double maximum {};
		for (const auto value : values)
		{
			if (!std::isfinite(value)) return std::numeric_limits<double>::infinity();
			maximum = std::max(maximum, std::abs(value));
		}
		return maximum;
	}

	// Takes the point at output, then starts the next Newton step or stops.
	[[gnu::always_inline]] void accept(const Stages& pointResidual, const Stages& pointTanh, double pointInputTanh, double pointError) noexcept
	{
		residual = pointResidual;
		stageTanh = pointTanh;
		inputTanh = pointInputTanh;
		error = pointError;
		if (iterations >= maximumIterations || !(error > convergenceTolerance))
		{
			done = true;
			return;
		}
		// Forward substitution with an affine dependence on delta[3] solves
		// the cyclic 4x4 Jacobian without a generic matrix factorization.
		Stages independent {}, dependent {};
		const auto diagonal = 1.0 + g * sechSquaredFromTanh(stageTanh[0]);
		independent[0] = -residual[0] / diagonal;
		dependent[0] = -g * k * sechSquaredFromTanh(inputTanh) / diagonal;
		for (std::size_t stage = 1; stage < 4; ++stage)
		{
			const auto coupling = g * sechSquaredFromTanh(stageTanh[stage - 1]);
			const auto scale = 1.0 / (1.0 + g * sechSquaredFromTanh(stageTanh[stage]));
			independent[stage] = (-residual[stage] + coupling * independent[stage - 1]) * scale;
			dependent[stage] = coupling * dependent[stage - 1] * scale;
		}
		const auto last = independent[3] / (1.0 - dependent[3]);
		for (std::size_t stage = 0; stage < 4; ++stage)
			step[stage] = independent[stage] + dependent[stage] * last;
		attempt = 0;
		damp();
	}

	void damp() noexcept
	{
		const auto damping = std::ldexp(1.0, -attempt);
		trial = output;
		for (std::size_t stage = 0; stage < 4; ++stage)
			trial[stage] += damping * step[stage];
	}
};
}

void NonlinearTptLadder::prepare(double newSampleRate) noexcept
{
	sampleRate = static_cast<float>(std::max(1.0, newSampleRate));
	// 5 ms attack, 150 ms release: holds a 20 Hz peak within a few percent.
	peakAttack = 1.0 - std::exp(-1.0 / (0.005 * static_cast<double>(sampleRate)));
	peakRelease = 1.0 - std::exp(-1.0 / (0.15 * static_cast<double>(sampleRate)));
	reset();
}

void NonlinearTptLadder::reset() noexcept
{
	previousFeedbackInput = 0.0;
	drivenPeak = 0.0;
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
	const auto cutoff = std::clamp(settings.cutoffHz, 2.5f, maximumCutoff);
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
			2.5f, sampleRate * 0.45f);
		const auto integrationGain = std::tan(std::numbers::pi_v<double> * cutoff / sampleRate)
			* ladderResonanceTuning(static_cast<double>(interpolate(previousSettings.resonance, settings.resonance))) / count;
		const NonlinearTptLadderSettings stepSettings {
			cutoff,
			interpolate(previousSettings.resonance, settings.resonance),
			interpolate(previousSettings.driveDecibels, settings.driveDecibels),
			settings.driveCompensation,
			{},
			interpolate(previousSettings.mode, settings.mode)
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
	followDrivenPeak(drivenInput);
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

	const auto mix = [&]
	{
		if (settings.mode <= -1.0f) return evaluation.output.back();
		std::array<double, 4> stageTanh {};
		for (std::size_t stage = 0; stage < stageTanh.size(); ++stage) stageTanh[stage] = std::tanh(evaluation.output[stage]);
		return ladderPoleMix(static_cast<double>(settings.mode), feedbackGain * ladderFeedbackAuthority(drivenPeak), feedbackInput, evaluation.output,
			ladderPoleMixKnee(static_cast<double>(driveGain)), std::tanh(feedbackInput), stageTanh);
	};
	const auto output = mix();
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
	const auto cutoff = std::clamp(settings.cutoffHz, 2.5f, sampleRate * 0.45f);
	const auto g = std::tan(std::numbers::pi_v<double> * cutoff / sampleRate)
		* ladderResonanceTuning(static_cast<double>(settings.resonance));
	const auto k = ladderFeedbackGain(static_cast<double>(settings.resonance));
	const auto driveGain = driveGainFor(settings.driveDecibels);
	const auto driven = std::clamp(static_cast<double>(input) * driveGain, -signalLimit, signalLimit);
	// Feed a bounded fraction of the driven input around the global feedback.
	// With zero input this is exactly the uncompensated feedback system.
	const auto excitation = driven + k * std::clamp(static_cast<double>(settings.inputFeedbackCompensation), 0.0, 0.5) * driven;
	followDrivenPeak(excitation);
	// tanh dominates the cost: each distinct argument is evaluated once per point, and the Newton step reuses
	// the tanh values from the residual at the current point.
	CoupledNewton newton;
	newton.start(integratorState, g, k, excitation, previousOutput);
	while (!newton.done)
	{
		auto values = newton.arguments();
		for (auto& value : values) value = std::tanh(value);
		newton.advance(values);
	}
	return completeCoupledStep(newton.output, newton.error, newton.iterations, newton.lineSearchTrials,
		excitation - k * newton.output[3], k, driveGain, settings, newton.inputTanh, newton.stageTanh);
}

float NonlinearTptLadder::completeCoupledStep(const std::array<double, 4>& output, double error, int iterations,
	std::uint64_t lineSearchTrials, double feedbackInput, double feedbackGain, float driveGain,
	const NonlinearTptLadderSettings& settings, double inputTanh, const std::array<double, 4>& stageTanh) noexcept
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
	const auto mode = static_cast<double>(settings.mode);
	// LP skips the tap arithmetic (and its pow/sqrt) entirely.
	const auto mixed = mode <= -1.0 ? output[3] : ladderPoleMix(mode, feedbackGain * ladderFeedbackAuthority(drivenPeak), feedbackInput,
		output, ladderPoleMixKnee(static_cast<double>(driveGain)), inputTanh, stageTanh);
	return static_cast<float>(settings.driveCompensation ? mixed / std::sqrt(driveGain) : mixed);
}
void NonlinearTptLadder::processCoupled(std::span<NonlinearTptLadder> ladders, std::span<const float> inputs,
	std::span<float> outputs, const NonlinearTptLadderSettings& settings) noexcept
{
	std::array<NonlinearTptLadder*, 4> pointers {};
	std::array<const NonlinearTptLadderSettings*, 4> shared {};
	const auto count = std::min(ladders.size(), pointers.size());
	for (std::size_t lane = 0; lane < count; ++lane)
	{
		pointers[lane] = &ladders[lane];
		shared[lane] = &settings;
	}
	processCoupled(std::span<NonlinearTptLadder* const>(pointers.data(), count), inputs.first(count), outputs.first(count),
		std::span<const NonlinearTptLadderSettings* const>(shared.data(), count));
	for (std::size_t lane = count; lane < ladders.size(); ++lane) outputs[lane] = ladders[lane].processCoupled(inputs[lane], settings);
}

void NonlinearTptLadder::processCoupled(std::span<NonlinearTptLadder* const> ladders, std::span<const float> inputs,
	std::span<float> outputs, std::span<const NonlinearTptLadderSettings* const> settings) noexcept
{
	// The batched solve shares tanh only; each lane's integration gain comes from its own sample rate. Ladders
	// prepared at different rates are nonetheless solved one by one, as before: the rates must be bit-identical
	// (not merely close) for a lane to match its own solve, so compare their bits.
	std::size_t lane {};
	const auto sameRate = [&](std::size_t first, std::size_t count)
	{
		for (auto index = first + 1; index < first + count; ++index)
			if (std::bit_cast<std::uint32_t>(ladders[index]->sampleRate) != std::bit_cast<std::uint32_t>(ladders[first]->sampleRate))
				return false;
		return true;
	};
	const auto batch = [&]<std::size_t Lanes>(std::integral_constant<std::size_t, Lanes>)
	{
		std::array<NonlinearTptLadder*, Lanes> group {};
		std::array<const NonlinearTptLadderSettings*, Lanes> groupSettings {};
		for (std::size_t index = 0; index < Lanes; ++index)
		{
			group[index] = ladders[lane + index];
			groupSettings[index] = settings[lane + index];
		}
		processCoupledLanes<Lanes>(group, inputs.data() + lane, outputs.data() + lane, groupSettings);
		lane += Lanes;
	};
	while (lane < ladders.size())
	{
		const auto remaining = ladders.size() - lane;
		if (remaining >= 4 && sameRate(lane, 4)) batch(std::integral_constant<std::size_t, 4> {});
		else if (remaining >= 2 && sameRate(lane, 2)) batch(std::integral_constant<std::size_t, 2> {});
		else batch(std::integral_constant<std::size_t, 1> {});
	}
}

template <std::size_t Lanes>
void NonlinearTptLadder::processCoupledLanes(const std::array<NonlinearTptLadder*, Lanes>& ladders, const float* inputs,
	float* outputs, const std::array<const NonlinearTptLadderSettings*, Lanes>& settings) noexcept
{
	// Each lane runs processCoupled's CoupledNewton; only tanh is shared across lanes (and is the vector kernel even
	// for one lane, so a lane's result does not depend on how lanes are grouped).
	// Per-lane coefficients; lanes sharing one settings object (a voice's unison layers) reuse lane 0's.
	LaneValues<Lanes> g {}, k {}, excitation {};
	std::array<float, Lanes> driveGain {};
	std::array<CoupledNewton, Lanes> newtons;
	for (std::size_t lane = 0; lane < Lanes; ++lane)
	{
		const auto& laneSettings = *settings[lane];
		if (lane > 0 && settings[lane] == settings[0] && std::bit_cast<std::uint32_t>(ladders[lane]->sampleRate)
			== std::bit_cast<std::uint32_t>(ladders[0]->sampleRate))
		{
			g[lane] = g[0];
			k[lane] = k[0];
		}
		else
		{
			const auto sampleRate = ladders[lane]->sampleRate;
			const auto cutoff = std::clamp(laneSettings.cutoffHz, 2.5f, sampleRate * 0.45f);
			g[lane] = std::tan(std::numbers::pi_v<double> * cutoff / sampleRate)
				* ladderResonanceTuning(static_cast<double>(laneSettings.resonance));
			k[lane] = ladderFeedbackGain(static_cast<double>(laneSettings.resonance));
		}
		driveGain[lane] = ladders[lane]->driveGainFor(laneSettings.driveDecibels);
		const auto driven = std::clamp(static_cast<double>(inputs[lane]) * driveGain[lane], -signalLimit, signalLimit);
		excitation[lane] = driven + k[lane] * std::clamp(static_cast<double>(laneSettings.inputFeedbackCompensation), 0.0, 0.5) * driven;
		ladders[lane]->followDrivenPeak(excitation[lane]);
		newtons[lane].start(ladders[lane]->integratorState, g[lane], k[lane], excitation[lane], ladders[lane]->previousOutput);
	}
	for (;;)
	{
		// All five tanh of every pending lane (feedback input, then the four stages) in as few vector calls as possible.
		SimdLaneValues<5 * Lanes> arguments {};
		bool pending {};
		for (std::size_t lane = 0; lane < Lanes; ++lane)
			if (!newtons[lane].done)
			{
				const auto laneArguments = newtons[lane].arguments();
				for (std::size_t index = 0; index < laneArguments.size(); ++index) arguments[5 * lane + index] = laneArguments[index];
				pending = true;
			}
		if (!pending) break;
		const auto tanhValues = tanhSimdLanes<5 * Lanes>(arguments);
		for (std::size_t lane = 0; lane < Lanes; ++lane)
			if (!newtons[lane].done)
			{
				CoupledNewton::Arguments laneValues {};
				for (std::size_t index = 0; index < laneValues.size(); ++index) laneValues[index] = tanhValues[5 * lane + index];
				newtons[lane].advance(laneValues);
			}
	}
	for (std::size_t lane = 0; lane < Lanes; ++lane)
	{
		const auto& newton = newtons[lane];
		outputs[lane] = ladders[lane]->completeCoupledStep(newton.output, newton.error, newton.iterations, newton.lineSearchTrials,
			excitation[lane] - k[lane] * newton.output[3], k[lane], driveGain[lane], *settings[lane],
			newton.inputTanh, newton.stageTanh);
	}
}
}
