#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <span>

namespace vekt::mono
{
struct NonlinearTptLadderSettings
{
	float cutoffHz { 1'000.0f };
	float resonance {};
	float driveDecibels {};
	bool driveCompensation {};
	float inputFeedbackCompensation {};
};

struct NonlinearTptLadderDiagnostics
{
	std::uint64_t samples {};
	std::uint64_t unconvergedSamples {};
	std::uint64_t nonFiniteSamples {};
	int maximumFeedbackIterations {};
	int maximumStageIterations {};
	float maximumResidual {};
	std::uint64_t coupledIterations {};
	std::uint64_t coupledLineSearchTrials {};
};

// Four-stage nonlinear ladder. Each stage applies a
// trapezoidal integrator to tanh(input) - tanh(output), while the instantaneous
// global feedback loop is solved with bounded Newton iterations.
class NonlinearTptLadder
{
public:
	void prepare(double newSampleRate) noexcept;
	void reset() noexcept;
	[[nodiscard]] float process(float input, const NonlinearTptLadderSettings& settings) noexcept;
	// Production solve of the four implicit stage equations.
	[[nodiscard]] float processCoupled(float input, const NonlinearTptLadderSettings& settings) noexcept;
	// Solves several ladders that share settings (e.g. one voice's unison layers) in lockstep. Each ladder keeps
	// its own convergence, line search and diagnostics exactly as processCoupled; for two or four ladders prepared
	// at the same sample rate, tanh is evaluated as a vector (Apple simd), which differs from libm by about 2 ulp.
	// Any other count, or ladders prepared at different rates, use processCoupled per ladder.
	static void processCoupled(std::span<NonlinearTptLadder> ladders, std::span<const float> inputs,
		std::span<float> outputs, const NonlinearTptLadderSettings& settings) noexcept;
	// The same batched solve for ladders that each have their own settings (e.g. different voices): lanes are
	// grouped four, then two, at a time; a single remaining lane uses processCoupled. Every lane keeps its own
	// convergence and diagnostics; only tanh is shared across lanes. Ladders must not repeat.
	static void processCoupled(std::span<NonlinearTptLadder* const> ladders, std::span<const float> inputs,
		std::span<float> outputs, std::span<const NonlinearTptLadderSettings* const> settings) noexcept;
	// Development-only bounded integration variant. Prepare at the rate of the
	// incoming samples; interpolate endpoints and preserve that rate's prewarp.
	[[nodiscard]] float processSubstepped(float input, const NonlinearTptLadderSettings& settings,
		int substeps) noexcept;
	[[nodiscard]] const NonlinearTptLadderDiagnostics& diagnostics() const noexcept { return solverDiagnostics; }
	// Offline analysis only: inject/read the four integrator states to finite-
	// difference the real coupled step. Call prepare first; no audio-thread use.
	void setAnalysisIntegratorState(const std::array<double, 4>& state) noexcept;
	[[nodiscard]] std::array<double, 4> analysisIntegratorState() const noexcept { return integratorState; }

private:
	struct Evaluation
	{
		std::array<double, 4> output {};
		double derivative {};
		double maximumResidual {};
		int maximumIterations {};
		bool converged { true };
	};

	[[nodiscard]] Evaluation evaluate(double input, double integrationGain) const noexcept;
	[[nodiscard]] float processStep(float input, const NonlinearTptLadderSettings& settings,
		double integrationGain) noexcept;
	template <std::size_t Lanes>
	static void processCoupledLanes(const std::array<NonlinearTptLadder*, Lanes>& ladders, const float* inputs,
		float* outputs, const std::array<const NonlinearTptLadderSettings*, Lanes>& settings) noexcept;
	// Shared end of a coupled step: diagnostics, the non-finite guard and the state update.
	[[nodiscard]] float completeCoupledStep(const std::array<double, 4>& output, double error, int iterations,
		std::uint64_t lineSearchTrials, double feedbackInput, float driveGain, bool driveCompensation) noexcept;

	// Drive in dB to linear gain, recomputed only when Drive changes (pow per sample is measurable).
	[[nodiscard]] float driveGainFor(float decibels) noexcept
	{
		if (std::abs(decibels - cachedDriveDecibels) > 0.0f)
		{
			cachedDriveDecibels = decibels;
			cachedDriveGain = std::pow(10.0f, decibels / 20.0f);
		}
		return cachedDriveGain;
	}

	float sampleRate { 48'000.0f };
	float cachedDriveDecibels {}, cachedDriveGain { 1.0f };
	double previousFeedbackInput {};
	std::array<double, 4> integratorState {};
	std::array<double, 4> previousOutput {};
	NonlinearTptLadderDiagnostics solverDiagnostics;
	float previousInput {};
	NonlinearTptLadderSettings previousSettings {};
	bool hasPreviousSettings {};
};
}