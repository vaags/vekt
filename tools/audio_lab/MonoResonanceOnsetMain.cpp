#include "../../plugins/vekt_mono/Source/NonlinearTptLadder.h"
#include "../../plugins/vekt_mono/Source/LadderResonance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numbers>

namespace
{
constexpr double rate = 48'000.0;
constexpr int excitationSamples = 24'000;
constexpr int tailSamples = 144'000;
constexpr int windowSamples = 9'600;

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
		"unconverged,nonfinite\n" << std::setprecision(12);
	for (int index = 0; index <= 100; ++index)
	{
		const auto resonance = static_cast<float>(0.90 + index * 0.001);
		// The voice now sends its unboosted output to the mix at all resonance values.
		constexpr float voiceGain = 1.0f;
		vekt::mono::NonlinearTptLadder ladder;
		ladder.prepare(rate);
		const vekt::mono::NonlinearTptLadderSettings settings { 1'000.0f, resonance, 0.0f };
		Window driven, early, late;
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
			if (sample >= excitationSamples + tailSamples - windowSamples)
				late.add(value, sample);
		}
		const auto& diagnostics = ladder.diagnostics();
		if (diagnostics.unconvergedSamples || diagnostics.nonFiniteSamples) return 1;
		output << resonance << ',' << vekt::mono::ladderFeedbackGain(static_cast<double>(resonance))
			<< ',' << voiceGain << ',' << driven.rms() << ',' << driven.peak
			<< ',' << early.rms() << ',' << late.rms() << ',' << late.peak
			<< ',' << late.frequency() << ',' << diagnostics.unconvergedSamples
			<< ',' << diagnostics.nonFiniteSamples << '\n';
	}
	std::cout << "rows=101 output=" << argv[1] << '\n';
	return output ? 0 : 1;
}