#include "../../plugins/vekt_mono/Source/NonlinearTptLadder.h"
#include "../../plugins/vekt_mono/Source/LadderResonance.h"

#include <vekt/audio_analysis/Measurements.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <string_view>

namespace
{
constexpr double rate = 48'000.0;
constexpr int settle = 48'000;
constexpr int measure = 48'000;
constexpr float bias = 0.1f;
constexpr float defaultProbe = 0.0001f;

// DC-biased numerical characterization only: this is not an AC-pumped musical
// drive measurement. At resonance=1, a probe can pull the autonomous oscillator;
// the paired-run difference is not an LTI small-signal transfer response.

struct Result
{
	double gain {};
	double phase {};
	double differenceRms {};
	vekt::mono::NonlinearTptLadderDiagnostics baseline, perturbed;
};

// Linearize the four implicit trapezoidal stages at their DC fixed point.
// At equilibrium all stage outputs equal excitation / (1 + k), because
// tanh(stage input) == tanh(stage output). The pole is derived from
// (1 + g*m)y[n] - (1 - g*m)y[n-1] = g*m*(u[n] + u[n-1]).
std::complex<double> dcLinearizedResponse(float resonance, float drive, bool qComp, double frequency)
{
	const auto k = vekt::mono::ladderFeedbackGain(static_cast<double>(resonance));
	const auto g = std::tan(std::numbers::pi * 1'000.0 / rate)
		* vekt::mono::ladderResonanceTuning(static_cast<double>(resonance));
	const auto driveGain = static_cast<double>(std::pow(10.0f, drive / 20.0f));
	const auto c = qComp ? static_cast<double>(0.20f * resonance) : 0.0;
	const auto equilibrium = (bias * driveGain * (1.0 + k * c)) / (1.0 + k);
	const auto tangent = std::tanh(equilibrium);
	const auto gm = g * (1.0 - tangent * tangent);
	const auto zInverse = std::exp(std::complex<double> {
		0.0, -2.0 * std::numbers::pi * frequency / rate });
	const auto stage = gm * (1.0 + zInverse)
		/ ((1.0 + gm) + (gm - 1.0) * zInverse);
	const auto cascade = stage * stage * stage * stage;
	return driveGain * (1.0 + k * c) * cascade / (1.0 + k * cascade);
}

Result measureGain(float resonance, float drive, bool qComp, double frequency, float probe)
{
	vekt::mono::NonlinearTptLadder baseline, perturbed;
	baseline.prepare(rate);
	perturbed.prepare(rate);
	const vekt::mono::NonlinearTptLadderSettings settings {
		1'000.0f, resonance, drive, false, qComp ? 0.20f * resonance : 0.0f
	};
	vekt::audio_analysis::SinusoidalProjector inputProjection(rate, frequency, settle);
	vekt::audio_analysis::SinusoidalProjector differenceProjection(rate, frequency, settle);
	double differenceSquareSum {};
	for (int sample = 0; sample < settle + measure; ++sample)
	{
		const auto excitation = probe * static_cast<float>(
			std::sin(2.0 * std::numbers::pi * frequency * sample / rate));
		const auto reference = baseline.processCoupled(bias, settings);
		const auto response = perturbed.processCoupled(bias + excitation, settings);
		if (sample < settle) continue;
		const auto difference = static_cast<double>(response) - reference;
		inputProjection.add(excitation);
		differenceProjection.add(difference);
		differenceSquareSum += difference * difference;
	}
	return { differenceProjection.result().magnitude() / inputProjection.result().magnitude(),
		vekt::audio_analysis::wrapPhase(differenceProjection.result().phaseRadians()
			- inputProjection.result().phaseRadians()),
		std::sqrt(differenceSquareSum / measure), baseline.diagnostics(), perturbed.diagnostics() };
}
}

int main(int argc, char** argv)
{
	if (argc < 2 || argc > 4)
	{
		std::cerr << "Usage: VektMonoIncrementalResonance output.csv [probe-amplitude [worst]]\n";
		return 64;
	}
	const bool worst = argc == 4 && std::string_view(argv[3]) == "worst";
	if (argc == 4 && !worst) return 64;
	char* end {};
	const auto probe = argc >= 3 ? std::strtof(argv[2], &end) : defaultProbe;
	if ((argc >= 3 && (end == argv[2] || *end != '\0'))
		|| !(probe > 0.0f && probe <= 0.001f) || !std::isfinite(probe)) return 64;
	std::ofstream output(argv[1]);
	if (!output) return 1;
	output << "resonance,drive_db,q_comp,bias,probe,frequency_hz,incremental_gain_db,difference_rms,"
		"linearized_gain_db,probe_minus_linearized_db,probe_phase_rad,linearized_phase_rad,phase_error_rad,"
		"baseline_unconverged,probe_unconverged,baseline_nonfinite,probe_nonfinite\n";
	output << std::setprecision(10);
	for (const auto resonance : { 0.5f, 0.8f, 0.95f, 1.0f })
		for (const auto drive : { 0.0f, 6.0f, 12.0f, 18.0f, 24.0f })
			for (const bool qComp : { false, true })
				for (const auto frequency : { 250.0, 500.0, 750.0, 900.0, 1'000.0,
					1'100.0, 1'250.0, 1'500.0, 2'000.0 })
				{
					if (worst && (resonance < 0.94f || resonance > 0.96f || drive < 18.0f
						|| (frequency != 750.0 && frequency != 900.0 && frequency != 1'000.0))) continue;
					const auto result = measureGain(resonance, drive, qComp, frequency, probe);
					const auto predicted = dcLinearizedResponse(resonance, drive, qComp, frequency);
					if (!std::isfinite(result.gain) || !std::isfinite(result.differenceRms)
						|| !std::isfinite(std::abs(predicted)) || !std::isfinite(result.phase)
						|| result.baseline.nonFiniteSamples || result.perturbed.nonFiniteSamples) return 1;
					const auto db = [](double gain) { return 20.0 * std::log10(std::max(gain, 1.0e-12)); };
					output << resonance << ',' << drive << ',' << qComp << ',' << bias << ',' << probe << ',' << frequency
						<< ',' << db(result.gain)
						<< ',' << result.differenceRms << ',' << db(std::abs(predicted))
						<< ',' << db(result.gain) - db(std::abs(predicted))
						<< ',' << result.phase << ',' << std::arg(predicted) << ','
						<< vekt::audio_analysis::wrapPhase(result.phase - std::arg(predicted))
						<< ',' << result.baseline.unconvergedSamples
						<< ',' << result.perturbed.unconvergedSamples << ','
						<< result.baseline.nonFiniteSamples << ',' << result.perturbed.nonFiniteSamples << '\n';
				}
	std::cout << "rows=" << (worst ? 12 : 360) << " output=" << argv[1] << '\n';
	return output ? 0 : 1;
}