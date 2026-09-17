#include "SignalSources.h"

#include <PluginProcessor.h>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <string_view>

namespace
{
struct Options final
{
	vekt::audio_lab::Source source { vekt::audio_lab::Source::sine };
	double sampleRate { 48'000.0 };
	int blockSize { 128 };
	double seconds { 1.0 };
	int mode {};
};

[[nodiscard]] bool valueFor(int& index, int argc, char** argv, std::string_view name, std::string& value)
{
	if (std::string_view(argv[index]) != name || index + 1 >= argc)
		return false;
	value = argv[++index];
	return true;
}

[[nodiscard]] bool parseOptions(int argc, char** argv, Options& options)
{
	for (int index = 1; index < argc; ++index)
	{
		std::string value;
		if (valueFor(index, argc, argv, "--source", value))
		{
			if (value == "silence") options.source = vekt::audio_lab::Source::silence;
			else if (value == "sine") options.source = vekt::audio_lab::Source::sine;
			else if (value == "sweep") options.source = vekt::audio_lab::Source::sweep;
			else if (value == "impulse") options.source = vekt::audio_lab::Source::impulse;
			else if (value == "noise") options.source = vekt::audio_lab::Source::noise;
			else if (value == "kick")
				options.source = vekt::audio_lab::Source::kick;
			else return false;
		}
		else if (valueFor(index, argc, argv, "--sample-rate", value)) options.sampleRate = std::stod(value);
		else if (valueFor(index, argc, argv, "--block-size", value)) options.blockSize = std::stoi(value);
		else if (valueFor(index, argc, argv, "--seconds", value)) options.seconds = std::stod(value);
		else if (valueFor(index, argc, argv, "--mode", value)) options.mode = std::clamp(std::stoi(value), 0, 5);
		else return false;
	}
	return options.sampleRate > 0.0 && options.blockSize > 0 && options.seconds > 0.0;
}
}

int main(int argc, char** argv)
{
	juce::ScopedJuceInitialiser_GUI juceInitialiser;
	Options options;
	if (!parseOptions(argc, argv, options))
	{
		std::cerr << "Usage: VektRavRender [--source silence|sine|sweep|impulse|noise|kick] "
					 "[--sample-rate Hz] [--block-size samples] [--seconds duration] [--mode 0-5]\n";
		return 64;
	}

	vekt::rav::PluginProcessor processor;
	auto* mode = processor.getParameters().getParameter(vekt::rav::parameters::mode);
	if (mode != nullptr)
		mode->setValueNotifyingHost(mode->convertTo0to1(static_cast<float>(options.mode)));
	processor.setNonRealtime(true);
	processor.prepareToPlay(options.sampleRate, options.blockSize);

	const auto totalSamples = static_cast<std::int64_t>(options.sampleRate * options.seconds);
	vekt::audio_lab::SignalSource source(options.source, options.sampleRate);
	juce::MidiBuffer midi;
	double sumSquares = 0.0;
	float peak = 0.0f;
	for (std::int64_t offset = 0; offset < totalSamples; offset += options.blockSize)
	{
		const auto blockSize = static_cast<int>(std::min<std::int64_t>(options.blockSize, totalSamples - offset));
		juce::AudioBuffer<float> buffer(2, blockSize);
		for (int sample = 0; sample < blockSize; ++sample)
		{
			const auto value = source.next(offset + sample);
			buffer.setSample(0, sample, value);
			buffer.setSample(1, sample, value);
		}
		processor.processBlock(buffer, midi);
		for (int sample = 0; sample < blockSize; ++sample)
		{
			const auto value = buffer.getSample(0, sample);
			sumSquares += static_cast<double>(value) * value;
			peak = std::max(peak, std::abs(value));
		}
	}

	const auto rms = std::sqrt(sumSquares / static_cast<double>(totalSamples));
	std::cout << "samples=" << totalSamples << " rms=" << rms
		<< " peak=" << peak << " latency=" << processor.getLatencySamples() << '\n';
	return 0;
}
