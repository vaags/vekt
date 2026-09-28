#include "../../plugins/vekt_mono/Source/NonlinearTptLadder.h"

#include <vekt/audio_analysis/Measurements.h>

#include <array>
#include <cmath>
#include <complex>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <string_view>

namespace
{
constexpr double rate = 48'000.0;
constexpr int settle = 48'000;
constexpr int window = 48'000;
// Integer-Hz pump and probe bins are orthogonal over the one-second window;
// 173 Hz and the 25-Hz-spaced probe grid avoid coincident pump harmonics.
// A periodically driven ladder is LTP: direct and translated bins are distinct
// incremental responses, not one ordinary LTI transfer function.
constexpr double pumpHz = 173.0;
constexpr float pumpAmplitude = 0.1f;

struct Measurement
{
	std::array<std::complex<double>, 5> transfer {};
	double differenceRms {};
	vekt::mono::NonlinearTptLadderDiagnostics baseline, perturbed;
};

Measurement measure(float resonance, float drive, bool qComp, double frequency, float probe)
{
	vekt::mono::NonlinearTptLadder baseline, perturbed;
	baseline.prepare(rate);
	perturbed.prepare(rate);
	const vekt::mono::NonlinearTptLadderSettings settings {
		1'000.0f, resonance, drive, false, qComp ? 0.20f * resonance : 0.0f
	};
	vekt::audio_analysis::SinusoidalProjector input(rate, frequency, settle);
	std::array<vekt::audio_analysis::SinusoidalProjector, 5> output {
		vekt::audio_analysis::SinusoidalProjector(rate, frequency - 2.0 * pumpHz, settle),
		vekt::audio_analysis::SinusoidalProjector(rate, frequency - pumpHz, settle),
		vekt::audio_analysis::SinusoidalProjector(rate, frequency, settle),
		vekt::audio_analysis::SinusoidalProjector(rate, frequency + pumpHz, settle),
		vekt::audio_analysis::SinusoidalProjector(rate, frequency + 2.0 * pumpHz, settle)
	};
	double squareSum {};
	for (int sample = 0; sample < settle + window; ++sample)
	{
		const auto pump = pumpAmplitude * static_cast<float>(
			std::sin(2.0 * std::numbers::pi * pumpHz * sample / rate));
		const auto excitation = probe * static_cast<float>(
			std::sin(2.0 * std::numbers::pi * frequency * sample / rate));
		const auto reference = baseline.processCoupled(pump, settings);
		const auto response = perturbed.processCoupled(pump + excitation, settings);
		if (sample < settle) continue;
		const auto difference = static_cast<double>(response) - reference;
		input.add(excitation);
		for (auto& bin : output) bin.add(difference);
		squareSum += difference * difference;
	}
	const auto in = input.result();
	const std::complex<double> inputComplex { in.real, in.imaginary };
	Measurement result;
	for (std::size_t i = 0; i < output.size(); ++i)
	{
		const auto bin = output[i].result();
		result.transfer[i] = std::complex<double> { bin.real, bin.imaginary } / inputComplex;
	}
	result.differenceRms = std::sqrt(squareSum / window);
	result.baseline = baseline.diagnostics();
	result.perturbed = perturbed.diagnostics();
	return result;
}
}

int main(int argc, char** argv)
{
	if (argc < 2 || argc > 3 || (argc == 3 && std::string_view(argv[2]) != "check"))
	{
		std::cerr << "Usage: VektMonoAcPumpResponse output.csv [check]\n";
		return 64;
	}
	const bool check = argc == 3;
	std::ofstream output(argv[1]);
	if (!output) return 1;
	output << "resonance,drive_db,q_comp,pump_hz,pump_amplitude,probe_hz,probe_amplitude,"
		"component_hz,sideband_order,gain_db,phase_rad,difference_rms,"
		"baseline_unconverged,probe_unconverged,baseline_nonfinite,probe_nonfinite\n";
	output << std::setprecision(10);
	int rows {};
	for (const auto resonance : { 0.5f, 0.8f, 0.95f })
		for (const auto drive : { 0.0f, 6.0f, 12.0f, 18.0f, 24.0f })
			for (const bool qComp : { false, true })
				for (int frequency = 400; frequency <= 1'400; frequency += 25)
				{
					if (check && !(resonance > 0.94f && drive >= 18.0f
						&& (frequency == 750 || frequency == 900 || frequency == 1'000))) continue;
					for (const auto probe : { 0.0001f, 0.00003f })
					{
						if (!check && probe < 0.0001f) continue;
						const auto result = measure(resonance, drive, qComp, frequency, probe);
						if (result.baseline.unconvergedSamples || result.perturbed.unconvergedSamples
							|| result.baseline.nonFiniteSamples || result.perturbed.nonFiniteSamples)
						{
							std::cerr << "Solver failure: resonance=" << resonance << " drive=" << drive
								<< " qComp=" << qComp << " frequency=" << frequency << '\n';
							return 1;
						}
						for (int order = -2; order <= 2; ++order)
						{
							// The two runs have identical pump, phase and initial state;
							// subtract before lock-in to remove the pump-only trajectory.
							const auto transfer = result.transfer[static_cast<std::size_t>(order + 2)];
							if (!std::isfinite(std::abs(transfer)) || !std::isfinite(result.differenceRms)) return 1;
							output << resonance << ',' << drive << ',' << qComp << ',' << pumpHz << ','
								<< pumpAmplitude << ',' << frequency << ',' << probe << ','
								<< frequency + order * pumpHz << ',' << order << ','
								<< vekt::audio_analysis::gainToDecibels(std::abs(transfer)) << ','
								<< std::arg(transfer) << ',' << result.differenceRms << ','
								<< result.baseline.unconvergedSamples << ',' << result.perturbed.unconvergedSamples
								<< ',' << result.baseline.nonFiniteSamples << ',' << result.perturbed.nonFiniteSamples << '\n';
							++rows;
						}
					}
				}
	std::cout << "rows=" << rows << " output=" << argv[1] << '\n';
	return output ? 0 : 1;
}