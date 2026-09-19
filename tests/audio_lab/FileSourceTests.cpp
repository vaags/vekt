#include "../../tools/audio_lab/FileSource.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

namespace
{
void writeFixture(const juce::File& file, juce::AudioFormat& format, int channels, int samples)
{
	std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
	auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions()
		.withSampleRate(24000).withNumChannels(channels).withBitsPerSample(16));
	REQUIRE(writer != nullptr);
	juce::AudioBuffer<float> audio(channels, samples);
	for (int channel = 0; channel < channels; ++channel)
		for (int sample = 0; sample < samples; ++sample)
			audio.setSample(channel, sample, channel == 0 ? 0.25f : -0.5f);
	REQUIRE(writer->writeFromAudioSampleBuffer(audio, 0, samples));
}
}

TEST_CASE("Audio Lab accepts only the four requested file formats", "[file-source]")
{
	const auto directory = juce::File::getSpecialLocation(juce::File::tempDirectory);
	for (const auto* extension : { "wav", "WAV", "mp3", "flac", "aiff", "aif" })
		REQUIRE(vekt::audio_lab::acceptsAudioFile(directory.getChildFile(juce::String("test.") + extension)));
	for (const auto* extension : { "m4a", "aac", "caf", "ogg", "txt" })
	{
		auto result = vekt::audio_lab::openAudioFile(directory.getChildFile(juce::String("test.") + extension));
		REQUIRE(result.reader == nullptr);
		REQUIRE(result.error.isNotEmpty());
	}
	for (const auto* extension : { ".wav", ".mp3", ".flac", ".aiff" })
	{
		juce::TemporaryFile file(extension);
		REQUIRE(file.getFile().replaceWithText("not an audio file"));
		REQUIRE(vekt::audio_lab::openAudioFile(file.getFile()).reader == nullptr);
	}
}

TEST_CASE("Audio Lab loops short mono and stereo files with rate correction", "[file-source]")
{
	juce::ScopedJuceInitialiser_GUI initialise;
	juce::WavAudioFormat wav;
	juce::FlacAudioFormat flac;
	juce::AiffAudioFormat aiff;
	for (auto* format : { static_cast<juce::AudioFormat*>(&wav), static_cast<juce::AudioFormat*>(&flac),
		static_cast<juce::AudioFormat*>(&aiff) })
		for (const auto channels : { 1, 2 })
		{
			juce::TemporaryFile fixture(format->getFileExtensions()[0]);
			writeFixture(fixture.getFile(), *format, channels, 37);
			auto result = vekt::audio_lab::openAudioFile(fixture.getFile());
			REQUIRE(result.error.isEmpty());
			REQUIRE(result.reader != nullptr);
			vekt::audio_lab::FileSource source;
			source.prepare(128, 48000);
			source.install(std::move(result.reader));
			REQUIRE(source.getDuration() == Catch::Approx(37.0 / 24000.0));
			juce::AudioBuffer<float> buffer(2, 144);
			for (int block = 0; block < 6; ++block)
			{
				juce::Thread::sleep(10); // Allow the real background reader to refill.
				buffer.clear();
				source.render({ &buffer, 8, 128 });
				if (block > 0)
					for (int sample = 8; sample < 136; ++sample)
					{
						REQUIRE(buffer.getSample(0, sample) == Catch::Approx(0.25f).margin(0.001));
						REQUIRE(buffer.getSample(1, sample) == Catch::Approx(channels == 1 ? 0.25f : -0.5f).margin(0.001));
					}
				REQUIRE(buffer.getSample(0, 0) == Catch::Approx(0.0f));
				REQUIRE(source.getPosition() < source.getDuration());
			}
			source.restart();
			REQUIRE(source.getPosition() == Catch::Approx(0.0));
			source.release();
			source.prepare(128, 44100);
			source.render({ &buffer, 0, 128 });
		}
}

TEST_CASE("Audio Lab rejects multichannel files and can replace a playing file", "[file-source]")
{
	juce::ScopedJuceInitialiser_GUI initialise;
	juce::WavAudioFormat wav;
	juce::TemporaryFile multichannel(".wav");
	writeFixture(multichannel.getFile(), wav, 4, 64);
	REQUIRE(vekt::audio_lab::openAudioFile(multichannel.getFile()).reader == nullptr);
	vekt::audio_lab::FileSource source;
	source.prepare(128, 48000);
	juce::AudioBuffer<float> buffer(2, 128);
	source.render({ &buffer, 0, 128 });
	REQUIRE(buffer.getMagnitude(0, 128) == Catch::Approx(0.0f));
	juce::TemporaryFile stereo(".wav");
	writeFixture(stereo.getFile(), wav, 2, 100);
	for (int i = 0; i < 3; ++i)
	{
		auto result = vekt::audio_lab::openAudioFile(stereo.getFile());
		REQUIRE(result.reader != nullptr);
		source.install(std::move(result.reader));
		source.render({ &buffer, 0, 128 });
	}
}

TEST_CASE("Audio Lab decodes MP3 and the AIF alias", "[file-source]")
{
	juce::ScopedJuceInitialiser_GUI initialise;
	const auto root = juce::File(__FILE__).getParentDirectory().getParentDirectory().getParentDirectory();
	const auto mp3 = root.getChildFile("external/JUCE/examples/Assets/Notifications/sounds/isntit.mp3");
	auto result = vekt::audio_lab::openAudioFile(mp3);
	REQUIRE(result.error.isEmpty());
	REQUIRE(result.reader != nullptr);
	juce::AudioBuffer<float> decoded(2, static_cast<int>(result.reader->lengthInSamples));
	REQUIRE(result.reader->read(&decoded, 0, decoded.getNumSamples(), 0, true, true));
	REQUIRE(decoded.getMagnitude(0, decoded.getNumSamples()) > 0.001f);
	vekt::audio_lab::FileSource source;
	source.prepare(128, 48000);
	source.install(std::move(result.reader));
	juce::AudioBuffer<float> output(2, 128);
	bool audible = false;
	const auto blocks = static_cast<int>(std::ceil(source.getDuration() * 48000.0 / 128.0)) * 2 + 4;
	for (int block = 0; block < blocks; ++block)
	{
		juce::Thread::sleep(3);
		source.render({ &output, 0, 128 });
		if (block > blocks / 2)
			audible = audible || output.getMagnitude(0, 128) > 0.001f;
	}
	REQUIRE(audible);
	juce::AiffAudioFormat aiff;
	juce::TemporaryFile alias(".aif");
	writeFixture(alias.getFile(), aiff, 1, 32);
	REQUIRE(vekt::audio_lab::openAudioFile(alias.getFile()).reader != nullptr);
}