#pragma once

#include "FlintParameters.h"

#include <vekt/dsp/LinearRamp.h>

#include <array>
#include <span>

namespace vekt::flint
{
// The shared Drive (docs/FLINT_VALIDATION.md, Shared stages): y = f(G x) · 0.5 / m(G), so a −6 dBFS peak keeps its
// level, with first-order antiderivative anti-aliasing. At Drive 0 the stage is skipped (bit-identical); it runs again
// as soon as Drive rises, crossfading from the dry path over 5 ms because the anti-aliasing delays the driven path by
// half a sample. A Drive Type change crossfades the two curves over 5 ms.
class DriveStage final
{
public:
	void prepare(double sampleRate) noexcept;
	void reset() noexcept;
	void setTarget(double drive, DriveType type) noexcept;
	void process(std::span<float> left, std::span<float> right) noexcept;
	[[nodiscard]] bool isBypassed() const noexcept;
	// Whether Drive still produces output of its own: anti-aliasing memory left. With silence at its input and none
	// left, its output is silence too, whatever its ramps are doing.
	[[nodiscard]] bool isRinging() const noexcept;

	// Input gain for Drive 0–1: 0 to +36 dB.
	[[nodiscard]] static double inputGain(double drive) noexcept;
	[[nodiscard]] static double shape(DriveType type, double x) noexcept;
	[[nodiscard]] static double antiderivative(DriveType type, double x) noexcept;
	// The curve's largest magnitude over the nominal input |u| <= 0.5 G; never 0 for G > 0.
	[[nodiscard]] static double nominalPeak(DriveType type, double gain) noexcept;

private:
	vekt::dsp::LinearRamp driveAmount;
	vekt::dsp::LinearRamp wetMix; // 0 dry … 1 driven
	vekt::dsp::LinearRamp typeMix; // 0 previous curve … 1 current curve
	DriveType currentType { DriveType::soft };
	DriveType previousType { DriveType::soft };
	std::array<double, 2> previousScaledInput {};
	std::array<bool, 2> primed {};
};
}
