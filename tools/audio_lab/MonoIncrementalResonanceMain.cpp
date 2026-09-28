#include "../../plugins/vekt_mono/Source/NonlinearTptLadder.h"

#include <vekt/audio_analysis/Measurements.h>

#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numbers>

namespace
{
constexpr double rate = 48'000.0;
constexpr int settle = 48'000;
constexpr int measure = 48'000;
constexpr float bias = 0.1f;
constexpr float probe = 0.001f;

struct Result
{
	double gain {};
	double differenceRms {};
	vekt::mono::NonlinearTptLadderDiagnostics baseline, perturbed;
};

Result measureGain(float resonance, float drive, bool qComp, double frequency)
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
		std::sqrt(differenceSquareSum / measure), baseline.diagnostics(), perturbed.diagnostics() };
}
}

int main(int argc, char** argv)
{
	if (argc != 2)
	{
		std::cerr << "Usage: VektMonoIncrementalResonance output.csv\n";
		return 64;
	}
	std::ofstream output(argv[1]);
	if (!output) return 1;
	output << "resonance,drive_db,q_comp,bias,probe,frequency_hz,incremental_gain_db,difference_rms,"
		"baseline_unconverged,probe_unconverged,baseline_nonfinite,probe_nonfinite\n";
	output << std::setprecision(10);
	for (const auto resonance : { 0.5f, 0.8f, 0.95f, 1.0f })
		for (const auto drive : { 0.0f, 6.0f, 12.0f, 18.0f, 24.0f })
			for (const bool qComp : { false, true })
				for (const auto frequency : { 250.0, 500.0, 750.0, 900.0, 1'000.0,
					1'100.0, 1'250.0, 1'500.0, 2'000.0 })
				{
					const auto result = measureGain(resonance, drive, qComp, frequency);
					if (!std::isfinite(result.gain) || !std::isfinite(result.differenceRms)
						|| result.baseline.nonFiniteSamples || result.perturbed.nonFiniteSamples) return 1;
					output << resonance << ',' << drive << ',' << qComp << ',' << bias << ',' << probe << ',' << frequency
						<< ',' << 20.0 * std::log10(std::max(result.gain, 1.0e-12))
						<< ',' << result.differenceRms << ',' << result.baseline.unconvergedSamples
						<< ',' << result.perturbed.unconvergedSamples << ','
						<< result.baseline.nonFiniteSamples << ',' << result.perturbed.nonFiniteSamples << '\n';
				}
	std::cout << "rows=360 output=" << argv[1] << '\n';
	return output ? 0 : 1;
}