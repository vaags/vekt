#include "../../tools/audio_lab/MonoRender.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_audio_formats/juce_audio_formats.h>

#include <bit>

namespace
{
double windowMeasurement(const juce::var& report, const juce::String& name, const juce::Identifier& measurement)
{
	if (const auto* windows = report.getProperty("windows", {}).getArray())
		for (const auto& window : *windows)
			if (window.getProperty("name", {}).toString() == name)
				if (const auto* channels = window.getProperty("channels", {}).getArray())
					return static_cast<double>((*channels)[0].getProperty(measurement, 0.0));
	return 0.0;
}
}

TEST_CASE("Mono Audio Lab fixtures use sample-positioned MIDI and parameter events", "[audio-lab][mono]")
{
	vekt::audio_lab::MonoRenderRequest filter;
	REQUIRE(vekt::audio_lab::makeMonoRenderFixture("filter-sweep", 48'000.0, 127, 1234, filter));
	REQUIRE(filter.events.front().type == vekt::audio_lab::MonoEventType::noteOn);
	REQUIRE(filter.events[1].type == vekt::audio_lab::MonoEventType::parameter);
	REQUIRE(filter.events[1].sample == 24'000);
	REQUIRE(filter.events.back().type == vekt::audio_lab::MonoEventType::noteOff);
	REQUIRE(filter.seed == 1234);

	vekt::audio_lab::MonoRenderRequest envelope;
	REQUIRE(vekt::audio_lab::makeMonoRenderFixture("envelope", 44'100.0, 64, 7, envelope));
	REQUIRE(envelope.events.front().sample == 2'205);
	REQUIRE(envelope.events.front().parameter == vekt::audio_lab::MonoParameter::ampRelease);
	REQUIRE(envelope.events[1].sample == 4'410);
	REQUIRE(envelope.events[1].type == vekt::audio_lab::MonoEventType::noteOn);
	REQUIRE_FALSE(vekt::audio_lab::makeMonoRenderFixture("unknown", 48'000.0, 64, 1, envelope));
}

TEST_CASE("Mono Audio Lab renders are deterministic and block-size invariant", "[audio-lab][mono][determinism]")
{
	vekt::audio_lab::MonoRenderRequest firstRequest, secondRequest, otherSeedRequest;
	REQUIRE(vekt::audio_lab::makeMonoRenderFixture("filter-sweep", 48'000.0, 31, 0x12345678u, firstRequest));
	REQUIRE(vekt::audio_lab::makeMonoRenderFixture("filter-sweep", 48'000.0, 257, 0x12345678u, secondRequest));
	REQUIRE(vekt::audio_lab::makeMonoRenderFixture("filter-sweep", 48'000.0, 31, 0x87654321u, otherSeedRequest));
	const auto first = vekt::audio_lab::renderMono(firstRequest);
	const auto second = vekt::audio_lab::renderMono(secondRequest);
	const auto otherSeed = vekt::audio_lab::renderMono(otherSeedRequest);
	REQUIRE(first.audio.getNumSamples() == second.audio.getNumSamples());
	bool seedChangedOutput = false;
	for (int channel = 0; channel < 2; ++channel)
		for (int sample = 0; sample < first.audio.getNumSamples(); ++sample)
		{
			REQUIRE(std::bit_cast<std::uint32_t>(first.audio.getSample(channel, sample))
				== std::bit_cast<std::uint32_t>(second.audio.getSample(channel, sample)));
			seedChangedOutput = seedChangedOutput
				|| std::bit_cast<std::uint32_t>(first.audio.getSample(channel, sample))
					!= std::bit_cast<std::uint32_t>(otherSeed.audio.getSample(channel, sample));
		}
	REQUIRE(seedChangedOutput);
}

TEST_CASE("Mono Audio Lab coupled render labels engine and remains deterministic", "[audio-lab][mono][ladder-coupled]")
{
	vekt::audio_lab::MonoRenderRequest request;
	REQUIRE(vekt::audio_lab::makeMonoRenderFixture("envelope", 48'000.0, 127, 42, request));
	const auto first = vekt::audio_lab::renderMono(request);
	const auto second = vekt::audio_lab::renderMono(request);
	REQUIRE(first.report.getProperty("engine", {}).toString() == "coupled");
	const auto* channels = first.report.getProperty("channels", {}).getArray();
	REQUIRE(channels != nullptr);
	REQUIRE(static_cast<double>((*channels)[0].getProperty("peak", 0.0)) > 0.001);
	for (int channel = 0; channel < 2; ++channel)
		for (int sample = 0; sample < first.audio.getNumSamples(); ++sample)
			REQUIRE(std::bit_cast<std::uint32_t>(first.audio.getSample(channel, sample))
				== std::bit_cast<std::uint32_t>(second.audio.getSample(channel, sample)));
}

TEST_CASE("Mono Audio Lab reports filter and envelope measurements", "[audio-lab][mono][measurements]")
{
	vekt::audio_lab::MonoRenderRequest filterRequest;
	REQUIRE(vekt::audio_lab::makeMonoRenderFixture("filter-sweep", 48'000.0, 128, 99, filterRequest));
	const auto filter = vekt::audio_lab::renderMono(filterRequest);
	REQUIRE(windowMeasurement(filter.report, "closed", "difference_rms") > 0.0);
	REQUIRE(windowMeasurement(filter.report, "open", "difference_rms")
		> windowMeasurement(filter.report, "closed", "difference_rms") * 1.5);

	vekt::audio_lab::MonoRenderRequest envelopeRequest;
	REQUIRE(vekt::audio_lab::makeMonoRenderFixture("envelope", 48'000.0, 128, 99, envelopeRequest));
	const auto envelope = vekt::audio_lab::renderMono(envelopeRequest);
	const auto measurements = envelope.report.getProperty("envelope", {});
	REQUIRE(static_cast<double>(measurements.getProperty("peak", 0.0)) > 0.01);
	REQUIRE(static_cast<double>(measurements.getProperty("attack_10_to_90_seconds", -1.0)) > 0.0);
	REQUIRE(static_cast<double>(measurements.getProperty("release_to_10_seconds", -1.0)) > 0.0);
	REQUIRE(windowMeasurement(envelope.report, "silence", "peak") == Catch::Approx(0.0));
}

TEST_CASE("Mono Audio Lab writes readable WAV and JSON outputs", "[audio-lab][mono][output]")
{
	vekt::audio_lab::MonoRenderRequest request;
	REQUIRE(vekt::audio_lab::makeMonoRenderFixture("envelope", 24'000.0, 97, 42, request));
	const auto result = vekt::audio_lab::renderMono(request);
	juce::TemporaryFile wav(".wav"), json(".json");
	REQUIRE(vekt::audio_lab::writeMonoRenderWav(wav.getFile(), result, request.sampleRate));
	REQUIRE(vekt::audio_lab::writeMonoRenderReport(json.getFile(), result));
	juce::WavAudioFormat format;
	std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(
		wav.getFile().createInputStream().release(), true));
	REQUIRE(reader != nullptr);
	REQUIRE(reader->sampleRate == Catch::Approx(request.sampleRate));
	REQUIRE(reader->numChannels == 2);
	REQUIRE(reader->lengthInSamples == request.totalSamples);
	const auto parsed = juce::JSON::parse(json.getFile());
	REQUIRE(parsed.isObject());
	REQUIRE(parsed.getProperty("product", {}).toString() == "mono");
	REQUIRE(parsed.getProperty("engine", {}).toString() == "coupled");
	REQUIRE(parsed.getProperty("fixture", {}).toString() == "envelope");
}
