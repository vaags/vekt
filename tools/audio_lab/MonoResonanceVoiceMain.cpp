#include "MonoRender.h"

#include <juce_events/juce_events.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>

int main(int argc, char** argv)
{
	if (argc != 2)
	{
		std::cerr << "Usage: VektMonoResonanceVoice output.csv\n";
		return 64;
	}
	juce::ScopedJuceInitialiser_GUI juceInitialiser;
	std::ofstream output(argv[1]);
	if (!output) return 1;
	output << std::setprecision(12)
		<< "resonance,q_comp,driven_voice_rms,driven_voice_peak,previous_ramp_gain,"
			"previous_ramp_expected_rms,previous_ramp_expected_peak\n";
	for (const auto compensated : { false, true })
		for (int step = 95; step <= 100; ++step)
		{
			vekt::audio_lab::MonoRenderRequest request;
			if (!vekt::audio_lab::makeMonoRenderFixture("q-comp-listen-95-sustain-drive-12-off",
				48'000.0, 128, 42, request)) return 1;
			request.settings.resonance = static_cast<float>(step) * 0.01f;
			request.settings.qCompensation = compensated;
			const auto render = vekt::audio_lab::renderMono(request);
			const auto& window = request.windows.front();
			double squares {}, peak {};
			for (int channel = 0; channel < 2; ++channel)
				for (auto sample = window.startSample; sample < window.endSample; ++sample)
				{
					const auto value = static_cast<double>(render.audio.getSample(channel, static_cast<int>(sample)));
					if (!std::isfinite(value)) return 1;
					squares += value * value;
					peak = std::max(peak, std::abs(value));
				}
			const auto rms = std::sqrt(squares / (2.0 * static_cast<double>(window.endSample - window.startSample)));
			const auto onset = std::clamp((step * 0.01 - 0.98) / 0.02, 0.0, 1.0);
			const auto oldGain = 1.0 + 0.6 * onset * onset * (3.0 - 2.0 * onset);
			output << request.settings.resonance << ',' << compensated << ',' << rms << ',' << peak << ','
				<< oldGain << ',' << rms * oldGain << ',' << peak * oldGain << '\n';
		}
	std::cout << "rows=12 output=" << argv[1] << '\n';
	return output ? 0 : 1;
}