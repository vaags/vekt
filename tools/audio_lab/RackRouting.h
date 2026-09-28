#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace vekt::audio_lab
{
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