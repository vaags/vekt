#pragma once

#include <array>
#include <complex>
#include <cstddef>

namespace vekt::flint
{
// Up to 16 resonant modes, each a complex one-pole resonator q <- p q + b u with p = r e^{i w}, in double; the output is
// the sum of the modes' Im(q) times their output gains (docs/FLINT_VALIDATION.md, Shared stages). Retuning or
// re-damping changes p without touching q, so the output never steps, and |q|^2 is each mode's exact energy. Modes above
// 20 kHz are dropped at every sample rate; a mode sleeps once its output and input fall below -120 dBFS.
class ModalBank final
{
public:
	static constexpr std::size_t maximumModes = 16;
	static constexpr double frequencyCeilingHz = 20'000.0;

	void prepare(double sampleRate) noexcept;
	void reset() noexcept;

	void setModeCount(std::size_t count) noexcept;
	[[nodiscard]] std::size_t modeCount() const noexcept { return count; }
	// A frequency at or above the ceiling silences the mode.
	void setMode(std::size_t mode, double frequencyHz, double t60Seconds) noexcept;
	void setInputGain(std::size_t mode, double gain) noexcept { inputGain[mode] = gain; }
	void setOutputGain(std::size_t mode, double gain) noexcept { outputGain[mode] = gain; }

	[[nodiscard]] double process(double input) noexcept;

	[[nodiscard]] std::complex<double> pole(std::size_t mode) const noexcept
	{
		return { poleReal[mode], poleImag[mode] };
	}
	[[nodiscard]] std::complex<double> state(std::size_t mode) const noexcept
	{
		return { stateReal[mode], stateImag[mode] };
	}
	[[nodiscard]] double inputGainOf(std::size_t mode) const noexcept { return inputGain[mode]; }
	[[nodiscard]] double energy() const noexcept;
	[[nodiscard]] bool isActive() const noexcept;

private:
	double sampleRateHz { 44'100.0 };
	std::size_t count {};
	std::array<double, maximumModes> poleReal {}, poleImag {};
	std::array<double, maximumModes> stateReal {}, stateImag {};
	std::array<double, maximumModes> inputGain {}, outputGain {};
	std::array<bool, maximumModes> audible {}; // below the ceiling
	std::array<bool, maximumModes> awake {};
};

// The strike build-up cap: the largest g <= 1 with |q + g d| <= max(2 |d|, |q|), for a mode in state q (at the strike's
// end) receiving a strike that alone would leave it in state d. A silent mode takes the full strike.
[[nodiscard]] double strikeScale(std::complex<double> state, std::complex<double> strike) noexcept;

}
