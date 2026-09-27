#pragma once

#include <cstddef>
#include <limits>

namespace vekt::audio_lab
{
// Diagnostic only: off-CPU time is compatible with preemption, but does not
// establish why the thread stopped running or qualify a real-time callback.
[[nodiscard]] constexpr bool wallOverrunWithCpuBelowDeadline(double wallUs, double cpuUs,
	double deadlineUs) noexcept
{
	return deadlineUs > 0.0 && deadlineUs < std::numeric_limits<double>::infinity()
		&& wallUs > deadlineUs && cpuUs >= 0.0 && cpuUs < deadlineUs;
}

[[nodiscard]] constexpr bool meetsMonoMeasuredTimingRule(double p99_9Us, double deadlineUs,
	std::size_t simulatedDeadlineExceeded) noexcept
{
	return deadlineUs > 0.0 && deadlineUs < std::numeric_limits<double>::infinity()
		&& p99_9Us >= 0.0 && p99_9Us < 0.75 * deadlineUs
		&& simulatedDeadlineExceeded == 0;
}
}