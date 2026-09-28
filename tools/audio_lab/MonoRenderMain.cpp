#include "MonoRender.h"

#include <juce_events/juce_events.h>

#include <iostream>
#include <string_view>

namespace
{
struct Options
{
	juce::String fixture { "filter-sweep" };
	double sampleRate { 48'000.0 };
	int blockSize { 128 };
	std::uint32_t seed { 0x4d6f6e6fu };
	juce::String wavPath;
	juce::String reportPath;
};

bool parse(int argc, char** argv, Options& options)
{
	try
	{
		for (int index = 1; index < argc; ++index)
		{
			if (index + 1 >= argc) return false;
			const std::string_view name(argv[index]);
			const auto value = juce::String(argv[++index]);
			if (name == "--fixture") options.fixture = value;
			else if (name == "--sample-rate") options.sampleRate = value.getDoubleValue();
			else if (name == "--block-size") options.blockSize = value.getIntValue();
			else if (name == "--seed") options.seed = static_cast<std::uint32_t>(std::stoul(value.toStdString()));
			else if (name == "--wav") options.wavPath = value;
			else if (name == "--report") options.reportPath = value;
			else return false;
		}
	}
	catch (const std::exception&)
	{
		return false;
	}
	return options.sampleRate > 0.0 && options.blockSize > 0
		&& options.wavPath.isNotEmpty() && options.reportPath.isNotEmpty();
}
}

int main(int argc, char** argv)
{
	juce::ScopedJuceInitialiser_GUI juceInitialiser;
	Options options;
	if (!parse(argc, argv, options))
	{
		std::cerr << "Usage: VektMonoRender --fixture filter-sweep|envelope|q-comp-body-off|q-comp-body-on|q-comp-tone-off|q-comp-tone-on|q-comp-drive-{0,6,12,18,24}-{off,on}|q-comp-listen-95-{sustain,bass,sweep}-drive-{12,18,24}-{off,on}|q-comp-listen-c05-95-{sustain,bass,sweep}-drive-{12,18,24}-{off,on} "
			"--wav path --report path [--sample-rate Hz] [--block-size samples] [--seed value]\n";
		return 64;
	}
	vekt::audio_lab::MonoRenderRequest request;
	if (!vekt::audio_lab::makeMonoRenderFixture(options.fixture, options.sampleRate,
		options.blockSize, options.seed, request))
	{
		std::cerr << "Unknown Mono fixture\n";
		return 64;
	}
	const auto result = vekt::audio_lab::renderMono(request);
	if (!vekt::audio_lab::writeMonoRenderWav(juce::File(options.wavPath), result, options.sampleRate)
		|| !vekt::audio_lab::writeMonoRenderReport(juce::File(options.reportPath), result))
	{
		std::cerr << "Unable to write Mono render outputs\n";
		return 1;
	}
	const auto* channel = result.report.getProperty("channels", {}).getArray();
	std::cout << "fixture=" << options.fixture << " samples=" << result.audio.getNumSamples();
	if (channel != nullptr && !channel->isEmpty())
		std::cout << " rms_left=" << (*channel)[0].getProperty("rms", 0.0).toString();
	std::cout << " wav=" << options.wavPath << " report=" << options.reportPath << '\n';
	return 0;
}
