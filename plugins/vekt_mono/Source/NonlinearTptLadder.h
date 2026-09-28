#pragma once

#include <array>
#include <cstdint>

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
	// Development-only bounded integration variant. Prepare at the rate of the
	// incoming samples; interpolate endpoints and preserve that rate's prewarp.
	[[nodiscard]] float processSubstepped(float input, const NonlinearTptLadderSettings& settings,
		int substeps) noexcept;
	[[nodiscard]] const NonlinearTptLadderDiagnostics& diagnostics() const noexcept { return solverDiagnostics; }

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

	float sampleRate { 48'000.0f };
	double previousFeedbackInput {};
	std::array<double, 4> integratorState {};
	std::array<double, 4> previousOutput {};
	NonlinearTptLadderDiagnostics solverDiagnostics;
	float previousInput {};
	NonlinearTptLadderSettings previousSettings {};
	bool hasPreviousSettings {};
};
}