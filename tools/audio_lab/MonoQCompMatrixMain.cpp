#include "MonoRender.h"

#include <vekt/audio_analysis/Measurements.h>

#include <juce_events/juce_events.h>

#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <span>
#include <string_view>

namespace
{
constexpr double rate = 48'000.0;
constexpr int blockSize = 128;
constexpr std::uint32_t seed = 42;

struct Measurement
{
	double rms {}, peak {}, crest {}, fundamental {};
};

[[nodiscard]] Measurement measure(const vekt::audio_lab::MonoRenderResult& result, double frequency)
{
	const auto start = static_cast<int>(rate * 0.45);
	const auto count = static_cast<int>(rate * 0.2);
	const std::span samples(result.audio.getReadPointer(0) + start, static_cast<std::size_t>(count));
	const auto statistics = vekt::audio_analysis::measureSamples<float>(samples);
	vekt::audio_analysis::SinusoidalProjector fundamental(rate, frequency,
		static_cast<std::size_t>(start));
	if (frequency > 0.0)
		for (const auto sample : samples) fundamental.add(sample);
	return { statistics.rms, statistics.peak, statistics.crestFactor,
		frequency > 0.0 ? fundamental.peakAmplitude() : 0.0 };
}

[[nodiscard]] vekt::audio_lab::MonoRenderRequest request(double cutoff, double resonance,
	double drive, std::string_view input, bool compensated)
{
	vekt::audio_lab::MonoRenderRequest result;
	result.fixture = "q-comp-matrix";
	result.sampleRate = rate;
	result.blockSize = blockSize;
	result.seed = seed;
	result.totalSamples = static_cast<std::int64_t>(rate * 0.7);
	result.settings = vekt::audio_lab::defaultMonoRenderSettings();
	result.settings.cutoff = static_cast<float>(cutoff);
	result.settings.resonance = static_cast<float>(resonance);
	result.settings.drive = static_cast<float>(drive);
	result.settings.qCompensation = compensated;
	result.settings.ampAttack = 0.0005f;
	result.settings.ampSustain = 1.0f;
	result.settings.level = { 0.7f, 0.0f, 0.0f };
	int note = 48;
	if (input == "sine")
	{
		result.settings.morph[0] = 0.0f;
		const auto frequency = cutoff * 0.25;
		note = static_cast<int>(std::round(69.0 + 12.0 * std::log2(frequency / 440.0)));
	}
	else if (input == "saw") result.settings.morph[0] = 2.0f;
	else
	{
		result.settings.level[0] = 0.0f;
		result.settings.noiseType = 1;
		result.settings.noiseLevel = 0.1f;
	}
	result.events.push_back({ 0, vekt::audio_lab::MonoEventType::noteOn,
		vekt::audio_lab::MonoParameter::cutoff, 1.0f, note });
	if (input == "tone")
	{
		result.settings.resonance = 1.0f;
		result.settings.qCompensation = false;
		result.events.push_back({ static_cast<std::int64_t>(rate * 0.1),
			vekt::audio_lab::MonoEventType::parameter,
			vekt::audio_lab::MonoParameter::noiseLevel, 0.0f });
		if (compensated)
			result.events.push_back({ static_cast<std::int64_t>(rate * 0.1),
				vekt::audio_lab::MonoEventType::parameter,
				vekt::audio_lab::MonoParameter::qCompensation, 1.0f });
	}
	return result;
}
}

int main(int argc, char** argv)
{
	if (argc != 2)
	{
		std::cerr << "Usage: VektMonoQCompMatrix output.csv\n";
		return 64;
	}
	juce::ScopedJuceInitialiser_GUI juceInitialiser;
	std::ofstream output(argv[1]);
	if (!output) return 1;
	output << "input,cutoff_hz,resonance,drive_db,q_comp,rms,peak,crest_factor,fundamental_hz,fundamental_peak\n";
	output << std::setprecision(10);
	int rows {};
	for (const auto input : { "sine", "saw", "noise", "tone" })
		for (const auto cutoff : { 100.0, 500.0, 2'000.0, 8'000.0 })
			for (const auto resonance : { 0.0, 0.5, 0.8, 0.98, 1.0 })
				for (const auto drive : { 0.0, 6.0, 12.0, 18.0, 24.0 })
				{
					if (std::string_view(input) == "tone" && resonance != 1.0) continue;
					for (const bool compensated : { false, true })
					{
						const auto renderRequest = request(cutoff, resonance, drive, input, compensated);
						const auto render = vekt::audio_lab::renderMono(renderRequest);
						// Noise has no single fundamental; the free-running tone is
						// projected at its requested cutoff, not pitch-tracked here.
						const auto frequency = std::string_view(input) == "tone" ? cutoff
							: std::string_view(input) == "noise" ? 0.0
							: vekt::mono::midiToHz(static_cast<float>(renderRequest.events.front().note));
						const auto result = measure(render, frequency);
						if (!std::isfinite(result.rms) || !std::isfinite(result.peak)
							|| !std::isfinite(result.crest) || !std::isfinite(result.fundamental)) return 1;
						output << input << ',' << cutoff << ',' << (std::string_view(input) == "tone" ? 1.0 : resonance)
							<< ',' << drive << ',' << compensated << ',' << result.rms << ',' << result.peak
							<< ',' << result.crest << ',' << frequency << ',' << result.fundamental << '\n';
						++rows;
					}
				}
	std::cout << "rows=" << rows << " output=" << argv[1] << '\n';
	return output ? 0 : 1;
}