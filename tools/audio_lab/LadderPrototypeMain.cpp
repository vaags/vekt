#include "LadderPrototype.h"

#include <juce_events/juce_events.h>

#include <iostream>
#include <string_view>

int main(int argc, char** argv)
{
	juce::ScopedJuceInitialiser_GUI juceInitialiser;
	double sampleRate = 48'000.0;
	int blockSize = 128;
	juce::String wavPath, reportPath;
	for (int index = 1; index < argc; ++index)
	{
		if (index + 1 >= argc) return 64;
		const std::string_view name(argv[index]);
		const auto value = juce::String(argv[++index]);
		if (name == "--sample-rate") sampleRate = value.getDoubleValue();
		else if (name == "--block-size") blockSize = value.getIntValue();
		else if (name == "--wav") wavPath = value;
		else if (name == "--report") reportPath = value;
		else return 64;
	}
	if (sampleRate <= 0.0 || blockSize <= 0 || wavPath.isEmpty() || reportPath.isEmpty())
	{
		std::cerr << "Usage: VektLadderPrototype --wav path --report path "
			"[--sample-rate Hz] [--block-size samples]\n";
		return 64;
	}
	const auto result = vekt::audio_lab::renderLadderPrototype(sampleRate, blockSize);
	if (!vekt::audio_lab::writeLadderPrototypeWav(juce::File(wavPath), result, sampleRate)
		|| !vekt::audio_lab::writeLadderPrototypeReport(juce::File(reportPath), result))
		return 1;
	std::cout << "models=current-delayed-feedback,four-stage-nonlinear-tpt-bounded-newton samples="
		<< result.audio.getNumSamples() << " wav=" << wavPath << " report=" << reportPath << '\n';
	return 0;
}