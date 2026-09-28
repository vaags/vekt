#include "../../plugins/vekt_mono/Source/NonlinearTptLadder.h"
#include "../../plugins/vekt_mono/Source/LadderResonance.h"
#include "MonoOnsetAnalysis.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <vector>

namespace
{
constexpr double rate = 48'000.0;
constexpr int excitationSamples = 24'000;
constexpr int tailSamples = 144'000;
constexpr int windowSamples = 9'600;
constexpr int weakExcitationSamples = 4'800;
constexpr int fitBinSamples = 960; // 20 ms RMS bins across the first 300 ms of zero input.
constexpr int fitBins = 15;

struct Window
{
	double squares {}, peak {};
	int first = -1, last = -1, crossings {};
	float previous {};
	void add(float value, int sample)
	{
		squares += static_cast<double>(value) * value;
		peak = std::max(peak, std::abs(static_cast<double>(value)));
		if (previous <= 0.0f && value > 0.0f)
		{
			if (first < 0) first = sample;
			last = sample;
			++crossings;
		}
		previous = value;
	}
	[[nodiscard]] double rms() const { return std::sqrt(squares / windowSamples); }
	[[nodiscard]] double frequency() const
	{
		return crossings > 1 ? (crossings - 1) * rate / (last - first) : 0.0;
	}
};
}

int main(int argc, char** argv)
{
	if (argc != 2)
	{
		std::cerr << "Usage: VektMonoResonanceOnset output.csv\n";
		return 64;
	}
	std::ofstream output(argv[1]);
	if (!output) return 1;
	output << "resonance,feedback_gain,voice_top_end_output_gain,driven_rms,driven_peak,"
		"early_zero_input_rms,late_zero_input_rms,late_zero_input_peak,late_frequency_hz,"
		"late_rms_drift_db_per_second,"
		"unconverged,nonfinite,weak_fit_lambda_per_second,weak_fit_r_squared,"
		"weak_fit_usable_bins,weak_fit_valid\n" << std::setprecision(12);
	// Coarse 94–100% coverage plus fine 98.31–98.49% points.
	// Late RMS is measured at 2.8–3.0 s, not extrapolated to infinite time.
	for (int index = 0; index < 79; ++index)
	{
		const auto resonance = static_cast<float>(index <= 43 ? 0.94 + index * 0.001
			: index <= 62 ? 0.983 + (index - 43) * 0.0001
			: 0.984 + (index - 62) * 0.001);
		// The voice now sends its unboosted output to the mix at all resonance values.
		constexpr float voiceGain = 1.0f;
		vekt::mono::NonlinearTptLadder ladder;
		ladder.prepare(rate);
		const vekt::mono::NonlinearTptLadderSettings settings { 1'000.0f, resonance, 0.0f };
		Window driven, early, penultimate, late;
		for (int sample = 0; sample < excitationSamples + tailSamples; ++sample)
		{
			const auto input = sample < excitationSamples ? 0.5f * static_cast<float>(
				std::sin(2.0 * std::numbers::pi * 317.0 * sample / rate)) : 0.0f;
			const auto value = ladder.processCoupled(input, settings);
			if (!std::isfinite(value)) return 1;
			if (sample >= excitationSamples - windowSamples && sample < excitationSamples)
				driven.add(value, sample);
			if (sample >= excitationSamples && sample < excitationSamples + windowSamples)
				early.add(value, sample);
			if (sample >= excitationSamples + tailSamples - 2 * windowSamples
				&& sample < excitationSamples + tailSamples - windowSamples)
				penultimate.add(value, sample);
			if (sample >= excitationSamples + tailSamples - windowSamples)
				late.add(value, sample);
		}
		const auto& diagnostics = ladder.diagnostics();
		if (diagnostics.unconvergedSamples || diagnostics.nonFiniteSamples) return 1;
		vekt::mono::NonlinearTptLadder weak;
		weak.prepare(rate);
		std::vector<double> bins;
		bins.reserve(fitBins);
		double binSquares {};
		for (int sample = 0; sample < weakExcitationSamples + fitBins * fitBinSamples; ++sample)
		{
			const auto input = sample < weakExcitationSamples ? 1.0e-4f * static_cast<float>(
				std::sin(2.0 * std::numbers::pi * 317.0 * sample / rate)) : 0.0f;
			const auto value = weak.processCoupled(input, settings);
			if (!std::isfinite(value)) return 1;
			if (sample < weakExcitationSamples) continue;
			binSquares += static_cast<double>(value) * value;
			if ((sample - weakExcitationSamples + 1) % fitBinSamples == 0)
			{
				bins.push_back(std::sqrt(binSquares / fitBinSamples));
				binSquares = 0.0;
			}
		}
		if (weak.diagnostics().unconvergedSamples || weak.diagnostics().nonFiniteSamples) return 1;
		const auto fit = vekt::audio_lab::fitOnset(bins, fitBinSamples / rate);
		// Do not interpret solver-floor RMS ratios as a sustain measurement.
		const auto hasLateTone = penultimate.rms() > 1.0e-5 && late.rms() > 1.0e-5;
		output << resonance << ',' << vekt::mono::ladderFeedbackGain(static_cast<double>(resonance))
			<< ',' << voiceGain << ',' << driven.rms() << ',' << driven.peak
			<< ',' << early.rms() << ',' << late.rms() << ',' << late.peak
			<< ',' << late.frequency() << ',';
		if (hasLateTone)
			output << 20.0 * std::log10(late.rms() / penultimate.rms()) / (windowSamples / rate);
		output << ',' << diagnostics.unconvergedSamples
			<< ',' << diagnostics.nonFiniteSamples << ',';
		if (fit.valid) output << fit.slopePerSecond << ',' << fit.rSquared;
		else output << ',';
		output << ',' << fit.usableBins << ',' << fit.valid << '\n';
	}
	std::cout << "rows=79 output=" << argv[1] << '\n';
	return output ? 0 : 1;
}