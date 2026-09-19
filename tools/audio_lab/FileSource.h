#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <atomic>
#include <cmath>
#include <memory>

namespace vekt::audio_lab
{
struct FileLoadResult
{
	juce::File file;
	std::unique_ptr<juce::AudioFormatReader> reader;
	juce::String error;
};

inline bool acceptsAudioFile(const juce::File& file)
{
	return file.hasFileExtension("wav;mp3;flac;aiff;aif");
}

// Called on the loader thread, never on the audio callback.
inline FileLoadResult openAudioFile(const juce::File& file)
{
	FileLoadResult result { file, {}, {} };
	if (!acceptsAudioFile(file))
	{
		result.error = "Supported formats: WAV, MP3, FLAC, AIFF/AIF.";
		return result;
	}
	juce::AudioFormatManager formats;
	formats.registerFormat(new juce::WavAudioFormat(), true);
	formats.registerFormat(new juce::MP3AudioFormat(), false);
	formats.registerFormat(new juce::FlacAudioFormat(), false);
	formats.registerFormat(new juce::AiffAudioFormat(), false);
	result.reader.reset(formats.createReaderFor(file));
	if (!result.reader)
		result.error = "Cannot decode this file. It may be missing, corrupt, or unsupported.";
	else if (result.reader->lengthInSamples <= 0 || !std::isfinite(result.reader->sampleRate)
		|| result.reader->sampleRate <= 0.0)
		result.error = "The file contains no valid audio.";
	else if (result.reader->numChannels < 1 || result.reader->numChannels > 2)
		result.error = "Only mono and stereo files are supported.";
	if (result.error.isNotEmpty())
		result.reader.reset();
	return result;
}

class FileSource final
{
public:
	FileSource() { readAhead.startThread(); }
	~FileSource()
	{
		transport.setSource(nullptr);
		readAhead.stopThread(-1);
	}

	void prepare(int blockSize, double rate)
	{
		const juce::ScopedLock guard(lock);
		transport.prepareToPlay(blockSize, rate);
	}
	void release()
	{
		const juce::ScopedLock guard(lock);
		transport.releaseResources();
	}
	void install(std::unique_ptr<juce::AudioFormatReader> reader)
	{
		jassert(reader != nullptr);
		const juce::ScopedLock guard(lock);
		transport.setSource(nullptr);
		channels = reader->numChannels;
		duration = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
		const auto rate = reader->sampleRate;
		source = std::make_unique<juce::AudioFormatReaderSource>(reader.release(), true);
		source->setLooping(true);
		transport.setSource(source.get(), 32768, &readAhead, rate, 2);
		transport.start();
		position.store(0.0);
	}
	void restart()
	{
		const juce::ScopedLock guard(lock);
		transport.setPosition(0.0);
		position.store(0.0);
	}
	void render(const juce::AudioSourceChannelInfo& info)
	{
		// Source replacement/preparation must never make the device callback wait.
		const juce::ScopedTryLock guard(lock);
		if (!guard.isLocked() || !source)
		{
			info.clearActiveBufferRegion();
			return;
		}
		transport.getNextAudioBlock(info);
		if (channels == 1 && info.buffer->getNumChannels() > 1)
			info.buffer->copyFrom(1, info.startSample, *info.buffer, 0, info.startSample, info.numSamples);
		position.store(std::fmod(transport.getCurrentPosition(), duration));
	}
	double getPosition() const { return position.load(); }
	double getDuration() const { return duration; } // message thread only

private:
	juce::CriticalSection lock;
	juce::TimeSliceThread readAhead { "Audio Lab file read-ahead" };
	std::unique_ptr<juce::AudioFormatReaderSource> source;
	juce::AudioTransportSource transport;
	unsigned int channels {};
	double duration {};
	std::atomic<double> position {};
};
}