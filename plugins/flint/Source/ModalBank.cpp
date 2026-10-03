#include "ModalBank.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace vekt::flint
{
namespace
{
// -120 dBFS in energy.
constexpr double sleepEnergy = 1.0e-12;
// ln(1000): an amplitude falls by 60 dB over T60.
constexpr double sixtyDecibelsNeper = 6.907755278982137;
}

void ModalBank::prepare(double sampleRate) noexcept
{
	sampleRateHz = sampleRate;
	reset();
}

void ModalBank::reset() noexcept
{
	stateReal = {};
	stateImag = {};
	awake = {};
}

void ModalBank::setModeCount(std::size_t newCount) noexcept
{
	count = std::min(newCount, maximumModes);
	for (auto mode = count; mode < maximumModes; ++mode)
	{
		stateReal[mode] = 0.0;
		stateImag[mode] = 0.0;
		awake[mode] = false;
	}
}

void ModalBank::setMode(std::size_t mode, double frequencyHz, double t60Seconds) noexcept
{
	audible[mode] = frequencyHz > 0.0 && frequencyHz < frequencyCeilingHz && frequencyHz < 0.5 * sampleRateHz;
	if (!audible[mode])
	{
		poleReal[mode] = 0.0;
		poleImag[mode] = 0.0;
		stateReal[mode] = 0.0;
		stateImag[mode] = 0.0;
		awake[mode] = false;
		return;
	}
	const auto radius = std::exp(-sixtyDecibelsNeper / (std::max(t60Seconds, 1.0e-4) * sampleRateHz));
	const auto angle = 2.0 * std::numbers::pi * frequencyHz / sampleRateHz;
	poleReal[mode] = radius * std::cos(angle);
	poleImag[mode] = radius * std::sin(angle);
}

double ModalBank::process(double input) noexcept
{
	const auto excited = input > 0.0 || input < 0.0;
	auto output = 0.0;
	for (std::size_t mode = 0; mode < count; ++mode)
	{
		if (!audible[mode] || !(awake[mode] || excited)) continue;
		const auto real = stateReal[mode], imag = stateImag[mode];
		const auto nextReal = poleReal[mode] * real - poleImag[mode] * imag + inputGain[mode] * input;
		const auto nextImag = poleReal[mode] * imag + poleImag[mode] * real;
		// Measured at the output: a mode whose output gain is high sleeps no earlier than -120 dBFS of output.
		const auto outputEnergy = (nextReal * nextReal + nextImag * nextImag) * outputGain[mode] * outputGain[mode];
		if (!excited && outputEnergy < sleepEnergy)
		{
			stateReal[mode] = 0.0;
			stateImag[mode] = 0.0;
			awake[mode] = false;
			continue;
		}
		stateReal[mode] = nextReal;
		stateImag[mode] = nextImag;
		awake[mode] = true;
		output += outputGain[mode] * nextImag;
	}
	return output;
}

double ModalBank::energy() const noexcept
{
	auto total = 0.0;
	for (std::size_t mode = 0; mode < count; ++mode)
		total += stateReal[mode] * stateReal[mode] + stateImag[mode] * stateImag[mode];
	return total;
}

bool ModalBank::isActive() const noexcept
{
	return std::any_of(awake.begin(), awake.begin() + static_cast<std::ptrdiff_t>(count), [](bool on) { return on; });
}

double strikeScale(std::complex<double> state, std::complex<double> strike) noexcept
{
	const auto strikeEnergy = std::norm(strike);
	if (strikeEnergy <= 0.0) return 1.0;
	const auto limit = std::max(2.0 * std::abs(strike), std::abs(state));
	// |q + g d|^2 = |d|^2 g^2 + 2 Re(conj(q) d) g + |q|^2 <= limit^2; g = 0 satisfies it, take the larger root.
	const auto half = std::real(std::conj(state) * strike);
	const auto constant = std::norm(state) - limit * limit;
	const auto root = (-half + std::sqrt(std::max(0.0, half * half - strikeEnergy * constant))) / strikeEnergy;
	return std::clamp(root, 0.0, 1.0);
}

}
