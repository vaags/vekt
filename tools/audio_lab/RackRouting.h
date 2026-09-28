#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace vekt::audio_lab
{
// Measure only plugin work; source generation and peak metering are excluded.
inline juce::int64 processMonoSource(juce::AudioBuffer<float>& block, juce::MidiBuffer& midi,
	juce::AudioProcessor& mono)
{
	const auto start = juce::Time::getHighResolutionTicks();
	mono.processBlock(block, midi);
	return juce::Time::getHighResolutionTicks() - start;
}

inline void processRackRoute(int route, juce::AudioBuffer<float>& block, juce::MidiBuffer& midi,
	juce::AudioProcessor& rav, juce::AudioProcessor& glimmer)
{
	switch (route)
	{
	case 1: rav.processBlock(block, midi); break;
	case 2: glimmer.processBlock(block, midi); break;
	case 3: rav.processBlock(block, midi); glimmer.processBlock(block, midi); break;
	case 4: glimmer.processBlock(block, midi); rav.processBlock(block, midi); break;
	default: break;
	}
}
}