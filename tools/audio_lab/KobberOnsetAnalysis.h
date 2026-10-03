#pragma once

#include <cstddef>
#include <cmath>
#include <vector>

namespace vekt::audio_lab
{
struct OnsetFit
{
	double slopePerSecond {}, rSquared {};
	int usableBins {};
	bool valid {};
};

// Fit log(RMS) only in the small-amplitude region. Times are bin midpoints
// relative to zero-input onset; invalid fits are not evidence of a threshold.
[[nodiscard]] inline OnsetFit fitOnset(const std::vector<double>& binRms,
	double binSeconds, double floor = 1.0e-7, double ceiling = 0.01)
{
	OnsetFit result;
	double sx {}, sy {}, sxx {}, sxy {}, syy {};
	bool started {};
	for (std::size_t i = 0; i < binRms.size(); ++i)
	{
		const auto amplitude = binRms[i];
		if (!std::isfinite(amplitude) || amplitude <= floor || amplitude >= ceiling)
		{
			if (started) break;
			continue;
		}
		started = true;
		const auto x = (static_cast<double>(i) + 0.5) * binSeconds;
		const auto y = std::log(amplitude);
		++result.usableBins;
		sx += x; sy += y; sxx += x * x; sxy += x * y; syy += y * y;
	}
	if (result.usableBins < 6) return result;
	const auto n = static_cast<double>(result.usableBins);
	const auto xx = n * sxx - sx * sx;
	const auto yy = n * syy - sy * sy;
	const auto xy = n * sxy - sx * sy;
	if (xx <= 0.0 || yy <= 0.0) return result;
	result.slopePerSecond = xy / xx;
	result.rSquared = xy * xy / (xx * yy);
	result.valid = std::isfinite(result.slopePerSecond) && std::isfinite(result.rSquared)
		&& result.rSquared >= 0.9;
	return result;
}
}