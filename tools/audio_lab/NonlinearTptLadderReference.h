#pragma once

#include <LadderResonance.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <numbers>
#include <span>

namespace vekt::audio_lab
{
struct NonlinearTptLadderReferenceSettings
{
	double cutoffHz { 1'000.0 };
	double resonance {};
	double driveDecibels {};
	bool driveCompensation {};
	double inputFeedbackCompensation {};
};

struct NonlinearTptLadderReferenceDiagnostics
{
	std::uint64_t hostSamples {};
	std::uint64_t internalSteps {};
	std::uint64_t unconvergedSteps {};
	std::uint64_t nonFiniteSteps {};
	int maximumFeedbackIterations {};
	int maximumStageIterations {};
	double maximumResidual {};
};

class NonlinearTptLadderOfflineReference final
{
public:
	void prepare(double newSampleRate, int newSubsteps) noexcept
	{
		sampleRate = std::max(1.0, newSampleRate);
		substeps = std::max(1, newSubsteps);
		reset();
	}

	void reset() noexcept
	{
		previousExternalInput = 0.0;
		previousSettings = {};
		hasPreviousSettings = false;
		previousFeedbackInput = 0.0;
		integratorState = {};
		solverDiagnostics = {};
	}

	// Optional raw internal samples are emitted before the reference's
	// host-rate return. Pass at least substeps elements to capture every step
	// for offline filtering prior to decimation.
	[[nodiscard]] double process(double input,
		const NonlinearTptLadderReferenceSettings& settings,
		std::span<double> internalOutput = {}) noexcept
	{
		if (!hasPreviousSettings)
		{
			previousSettings = settings;
			hasPreviousSettings = true;
		}
		double output {};
		double outputDriveGain { 1.0 };
		for (int substep = 0; substep < substeps; ++substep)
		{
			const auto fraction = static_cast<double>(substep + 1) / static_cast<double>(substeps);
			const auto interpolatedInput = previousExternalInput
				+ fraction * (input - previousExternalInput);
			const auto cutoff = std::clamp(previousSettings.cutoffHz
				+ fraction * (settings.cutoffHz - previousSettings.cutoffHz),
				10.0, sampleRate * 0.45);
			const auto resonance = std::clamp(previousSettings.resonance
				+ fraction * (settings.resonance - previousSettings.resonance), 0.0, 1.0);
			const auto driveDecibels = previousSettings.driveDecibels
				+ fraction * (settings.driveDecibels - previousSettings.driveDecibels);
			const auto hostIntegrationGain = std::tan(std::numbers::pi * cutoff / sampleRate);
			// wc = 2 * sampleRate * hostIntegrationGain; at N times the rate,
			// trapezoidal integration uses wc / (2 * N * sampleRate).
			const auto integrationGain = hostIntegrationGain * mono::ladderResonanceTuning(resonance)
				/ static_cast<double>(substeps);
			const auto feedbackGain = mono::ladderFeedbackGain(resonance);
			const auto driveGain = std::pow(10.0, driveDecibels / 20.0);
			const auto compensation = std::clamp(previousSettings.inputFeedbackCompensation
				+ fraction * (settings.inputFeedbackCompensation - previousSettings.inputFeedbackCompensation), 0.0, 0.5);
			output = processInternal(interpolatedInput * driveGain, integrationGain, feedbackGain, compensation);
			if (static_cast<std::size_t>(substep) < internalOutput.size())
				internalOutput[static_cast<std::size_t>(substep)] = output;
			outputDriveGain = driveGain;
		}
		previousExternalInput = input;
		previousSettings = settings;
		++solverDiagnostics.hostSamples;
		return settings.driveCompensation ? output / std::sqrt(outputDriveGain) : output;
	}

	[[nodiscard]] const NonlinearTptLadderReferenceDiagnostics& diagnostics() const noexcept
	{
		return solverDiagnostics;
	}

private:
	struct Evaluation
	{
		std::array<double, 4> output {};
		double derivative {};
		double maximumResidual {};
		int maximumIterations {};
		bool converged { true };
	};

	static constexpr int maximumFeedbackIterations = 32;
	static constexpr int maximumStageIterations = 32;
	static constexpr double convergenceTolerance = 1.0e-13;
	static constexpr double signalLimit = 24.0;

	[[nodiscard]] static double sechSquared(double value) noexcept
	{
		const auto tangent = std::tanh(value);
		return std::max(0.0, 1.0 - tangent * tangent);
	}

	[[nodiscard]] Evaluation evaluate(double input, double integrationGain) const noexcept
	{
		Evaluation evaluation;
		auto stageInput = input;
		auto derivative = 1.0;
		for (std::size_t stage = 0; stage < evaluation.output.size(); ++stage)
		{
			auto output = std::clamp(integratorState[stage]
				+ integrationGain * (std::tanh(stageInput) - std::tanh(integratorState[stage])),
				-signalLimit, signalLimit);
			auto lower = integratorState[stage] - 2.0 * integrationGain;
			auto upper = integratorState[stage] + 2.0 * integrationGain;
			bool converged = false;
			int iteration = 0;
			double residual {};
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
				output = next > lower && next < upper && std::abs(next - output) <= 0.5 * (upper - lower)
					? next : lower + 0.5 * (upper - lower);
			}
			residual = output - integratorState[stage]
				- integrationGain * (std::tanh(stageInput) - std::tanh(output));
			converged = converged || std::abs(residual) <= convergenceTolerance;
			evaluation.maximumResidual = std::max(evaluation.maximumResidual, std::abs(residual));
			evaluation.maximumIterations = std::max(evaluation.maximumIterations,
				iteration + (converged ? 1 : 0));
			evaluation.converged = evaluation.converged && converged;
			derivative *= integrationGain * sechSquared(stageInput)
				/ (1.0 + integrationGain * sechSquared(output));
			evaluation.output[stage] = output;
			stageInput = output;
		}
		evaluation.derivative = derivative;
		return evaluation;
	}

	[[nodiscard]] double processInternal(double drivenInput, double integrationGain,
		double feedbackGain, double inputFeedbackCompensation) noexcept
	{
		const auto boundedInput = std::clamp(drivenInput, -signalLimit, signalLimit);
		const auto excitation = boundedInput * (1.0 + feedbackGain * inputFeedbackCompensation);
		const auto span = feedbackGain * (signalLimit + 2.0 * integrationGain);
		auto lower = excitation - span;
		auto upper = excitation + span;
		auto feedbackInput = std::clamp(previousFeedbackInput, lower, upper);
		Evaluation evaluation;
		bool converged = false;
		int iteration = 0;
		double residual {};
		for (; iteration < maximumFeedbackIterations; ++iteration)
		{
			evaluation = evaluate(feedbackInput, integrationGain);
			residual = (feedbackInput - excitation) + feedbackGain * evaluation.output.back();
			if (std::abs(residual) <= convergenceTolerance)
			{
				converged = evaluation.converged;
				break;
			}
			if (residual > 0.0) upper = feedbackInput;
			else lower = feedbackInput;
			const auto slope = 1.0 + feedbackGain * evaluation.derivative;
			const auto next = feedbackInput - residual / slope;
			feedbackInput = next > lower && next < upper
				&& std::abs(next - feedbackInput) <= 0.5 * (upper - lower)
				? next : lower + 0.5 * (upper - lower);
		}
		if (!converged)
		{
			evaluation = evaluate(feedbackInput, integrationGain);
			residual = (feedbackInput - excitation) + feedbackGain * evaluation.output.back();
			converged = evaluation.converged && std::abs(residual) <= convergenceTolerance;
		}

		++solverDiagnostics.internalSteps;
		solverDiagnostics.maximumFeedbackIterations = std::max(
			solverDiagnostics.maximumFeedbackIterations, iteration + (converged ? 1 : 0));
		solverDiagnostics.maximumStageIterations = std::max(
			solverDiagnostics.maximumStageIterations, evaluation.maximumIterations);
		solverDiagnostics.maximumResidual = std::max(solverDiagnostics.maximumResidual,
			std::max(std::abs(residual), evaluation.maximumResidual));
		if (!converged) ++solverDiagnostics.unconvergedSteps;

		const auto output = evaluation.output.back();
		if (!std::isfinite(output))
		{
			++solverDiagnostics.nonFiniteSteps;
			integratorState = {};
			previousFeedbackInput = 0.0;
			return 0.0;
		}
		for (std::size_t stage = 0; stage < integratorState.size(); ++stage)
			integratorState[stage] = std::clamp(2.0 * evaluation.output[stage] - integratorState[stage],
				-signalLimit, signalLimit);
		previousFeedbackInput = feedbackInput;
		return output;
	}

	double sampleRate { 48'000.0 };
	int substeps { 16 };
	double previousExternalInput {};
	NonlinearTptLadderReferenceSettings previousSettings;
	bool hasPreviousSettings {};
	double previousFeedbackInput {};
	std::array<double, 4> integratorState {};
	NonlinearTptLadderReferenceDiagnostics solverDiagnostics;
};

[[nodiscard]] inline std::complex<double> nonlinearTptLadderContinuousResponse(
	double sampleRate, double frequencyHz, const NonlinearTptLadderReferenceSettings& settings) noexcept
{
	const auto cutoff = std::clamp(settings.cutoffHz, 10.0, sampleRate * 0.45);
	const auto warpedCutoffRadians = 2.0 * sampleRate
		* std::tan(std::numbers::pi * cutoff / sampleRate) * mono::ladderResonanceTuning(settings.resonance);
	const std::complex<double> s { 0.0, 2.0 * std::numbers::pi * frequencyHz };
	const auto stage = warpedCutoffRadians / (s + warpedCutoffRadians);
	const auto cascade = stage * stage * stage * stage;
	const auto feedbackGain = mono::ladderFeedbackGain(settings.resonance);
	const auto driveGain = std::pow(10.0, settings.driveDecibels / 20.0);
	const auto wrapperGain = settings.driveCompensation ? std::sqrt(driveGain) : driveGain;
	return wrapperGain * (1.0 + feedbackGain * std::clamp(settings.inputFeedbackCompensation, 0.0, 0.5))
		* cascade / (1.0 + feedbackGain * cascade);
}

[[nodiscard]] inline std::complex<double> nonlinearTptLadderBilinearMappedResponse(
	double sampleRate, double frequencyHz, const NonlinearTptLadderReferenceSettings& settings) noexcept
{
	const auto cutoff = std::clamp(settings.cutoffHz, 10.0, sampleRate * 0.45);
	const auto warpedCutoffRadians = 2.0 * sampleRate
		* std::tan(std::numbers::pi * cutoff / sampleRate) * mono::ladderResonanceTuning(settings.resonance);
	const auto warpedProbeRadians = 2.0 * sampleRate
		* std::tan(std::numbers::pi * frequencyHz / sampleRate);
	const std::complex<double> s { 0.0, warpedProbeRadians };
	const auto stage = warpedCutoffRadians / (s + warpedCutoffRadians);
	const auto cascade = stage * stage * stage * stage;
	const auto feedbackGain = mono::ladderFeedbackGain(settings.resonance);
	const auto driveGain = std::pow(10.0, settings.driveDecibels / 20.0);
	const auto wrapperGain = settings.driveCompensation ? std::sqrt(driveGain) : driveGain;
	return wrapperGain * (1.0 + feedbackGain * std::clamp(settings.inputFeedbackCompensation, 0.0, 0.5))
		* cascade / (1.0 + feedbackGain * cascade);
}

[[nodiscard]] inline std::complex<double> nonlinearTptLadderDiscreteResponse(
	double sampleRate, double frequencyHz, const NonlinearTptLadderReferenceSettings& settings) noexcept
{
	const auto cutoff = std::clamp(settings.cutoffHz, 10.0, sampleRate * 0.45);
	const auto integrationGain = std::tan(std::numbers::pi * cutoff / sampleRate)
		* mono::ladderResonanceTuning(settings.resonance);
	const auto zInverse = std::exp(std::complex<double> {
		0.0, -2.0 * std::numbers::pi * frequencyHz / sampleRate });
	const auto stage = integrationGain * (1.0 + zInverse)
		/ ((1.0 + integrationGain) + (integrationGain - 1.0) * zInverse);
	const auto cascade = stage * stage * stage * stage;
	const auto feedbackGain = mono::ladderFeedbackGain(settings.resonance);
	const auto driveGain = std::pow(10.0, settings.driveDecibels / 20.0);
	const auto wrapperGain = settings.driveCompensation ? std::sqrt(driveGain) : driveGain;
	return wrapperGain * (1.0 + feedbackGain * std::clamp(settings.inputFeedbackCompensation, 0.0, 0.5))
		* cascade / (1.0 + feedbackGain * cascade);
}
}