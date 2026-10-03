#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace vekt::audio_lab
{
// The instruments the lab plays, in its Instrument menu order.
enum class Instrument
{
	mono,
	flint
};
inline constexpr int instrumentCount = 2;

// Plays the selected instrument into the block and returns the time it took: only plugin work, not source generation
// or peak metering.
inline juce::int64 processInstrument(juce::AudioBuffer<float>& block, juce::MidiBuffer& midi,
	juce::AudioProcessor& instrument)
{
	const auto start = juce::Time::getHighResolutionTicks();
	instrument.processBlock(block, midi);
	return juce::Time::getHighResolutionTicks() - start;
}

// Room for silenceInstrument's events, reserved off the audio thread.
inline constexpr int silenceEventBytes = 16 * 3 * 8;

// The instrument the lab stops playing gets All Sound Off on every channel, so notes it held do not sound again when
// it is chosen later. Its output is discarded. `scratch` needs silenceEventBytes reserved, so nothing allocates.
inline void silenceInstrument(juce::AudioProcessor& instrument, juce::AudioBuffer<float>& block,
	juce::MidiBuffer& scratch)
{
	scratch.clear();
	for (auto channel = 1; channel <= 16; ++channel) scratch.addEvent(juce::MidiMessage::allSoundOff(channel), 0);
	instrument.processBlock(block, scratch);
	scratch.clear();
	block.clear();
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