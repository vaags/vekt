#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace vekt::audio_lab
{
struct LabSettings
{
	int source {};
	int octave {};
	int selectedTab { 1 };
	int rackRoute { 3 };
	juce::String midiInputIdentifier;
	juce::String audioFilePath;
	bool outputArmed {};
	int instrument {}; // Instrument (RackRouting.h): 0 Mono, 1 Flint
};

inline LabSettings readLabSettings(const juce::ValueTree& state)
{
	return {
		juce::jlimit(0, 9, static_cast<int>(state.getProperty("source", 0))),
		juce::jlimit(-3, 3, static_cast<int>(state.getProperty("octave", 0))),
		juce::jlimit(1, 4, static_cast<int>(state.getProperty("selectedTab", 1))),
		juce::jlimit(0, 4, static_cast<int>(state.getProperty("rackRoute", 3))),
		state.getProperty("midiInput", {}).toString(),
		state.getProperty("audioFilePath", {}).toString(),
		static_cast<bool>(state.getProperty("outputArmed", false)),
		juce::jlimit(0, 1, static_cast<int>(state.getProperty("instrument", 0)))
	};
}

inline void writeLabSettings(juce::ValueTree& state, const LabSettings& settings)
{
	state.setProperty("source", juce::jlimit(0, 9, settings.source), nullptr);
	state.setProperty("octave", juce::jlimit(-3, 3, settings.octave), nullptr);
	state.setProperty("selectedTab", juce::jlimit(1, 4, settings.selectedTab), nullptr);
	state.setProperty("rackRoute", juce::jlimit(0, 4, settings.rackRoute), nullptr);
	state.setProperty("midiInput", settings.midiInputIdentifier, nullptr);
	state.setProperty("audioFilePath", settings.audioFilePath, nullptr);
	state.setProperty("outputArmed", settings.outputArmed, nullptr);
	state.setProperty("instrument", juce::jlimit(0, 1, settings.instrument), nullptr);
}
}
