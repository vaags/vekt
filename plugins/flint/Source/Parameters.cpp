#include <vekt/flint/Parameters.h>

#include <vekt/flint/Models.h>
#include <vekt/plugin_support/QualitySelection.h>

#include "FlintParameters.h"

#include <cmath>

namespace vekt::flint::parameters
{
namespace
{
juce::StringArray modelNames(Mode selected)
{
	juce::StringArray names;
	for (const auto& entry : modelsOf(selected)) names.add(entry.name);
	return names;
}

void addPercent(juce::AudioProcessorValueTreeState::ParameterLayout& layout, const char* identifier,
    const juce::String& name, float defaultValue)
{
	layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { identifier, version }, name,
	    juce::NormalisableRange<float> { 0.0f, 100.0f, 0.01f }, defaultValue,
	    juce::AudioParameterFloatAttributes {}.withLabel("%")));
}

constexpr auto lowestPitch = static_cast<float>(fullPitchRange.lowest);
constexpr auto highestPitch = static_cast<float>(fullPitchRange.highest);

juce::AudioParameterChoiceAttributes fixed() { return juce::AudioParameterChoiceAttributes {}.withAutomatable(false); }
}

juce::String pitchText(float note)
{
	const auto rounded = std::round(note);
	const auto cents = juce::roundToInt((note - rounded) * 100.0f);
	auto text = juce::MidiMessage::getMidiNoteName(static_cast<int>(rounded), true, true, 4);
	if (cents != 0) text << (cents > 0 ? " +" : " ") << cents << " ct";
	return text;
}

float pitchFromText(const juce::String& text)
{
	const auto trimmed = text.trim();
	if (trimmed.containsOnly("0123456789.-+ ")) return juce::jlimit(lowestPitch, highestPitch, trimmed.getFloatValue());
	// A note name with optional sharp and octave, then optional cents: "D#2", "D#2 +12 ct".
	static const juce::StringArray names { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
	const auto token = trimmed.upToFirstOccurrenceOf(" ", false, false).toUpperCase();
	const auto hasSharp = token.length() > 1 && token[1] == '#';
	const auto name = token.substring(0, hasSharp ? 2 : 1);
	const auto index = names.indexOf(name);
	if (index < 0) return 33.0f;
	const auto octave = token.substring(hasSharp ? 2 : 1).getIntValue();
	const auto cents =
	    trimmed.fromFirstOccurrenceOf(" ", false, false).retainCharacters("0123456789-+").getFloatValue();
	return juce::jlimit(lowestPitch, highestPitch, static_cast<float>(12 * (octave + 1) + index) + cents / 100.0f);
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
	juce::AudioProcessorValueTreeState::ParameterLayout layout;
	layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { pitch, version }, "Pitch",
	    juce::NormalisableRange<float> { lowestPitch, highestPitch, 0.01f }, 33.0f,
	    juce::AudioParameterFloatAttributes {}
	        .withStringFromValueFunction([](float value, int) { return pitchText(value); })
	        .withValueFromStringFunction([](const juce::String& text) { return pitchFromText(text); })));
	addPercent(layout, attack, "Attack", 30.0f);
	addPercent(layout, decay, "Decay", 40.0f);
	addPercent(layout, tone, "Tone", 50.0f);
	addPercent(layout, drive, "Drive", 0.0f);
	layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { driveType, version }, "Drive Type",
	    driveTypes.names(), 0, fixed()));
	layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { level, version }, "Level",
	    juce::NormalisableRange<float> { -48.0f, 12.0f, 0.01f }, 0.0f,
	    juce::AudioParameterFloatAttributes {}.withLabel("dB")));
	addPercent(layout, velocity, "Velocity", 100.0f);
	addPercent(layout, variation, "Variation", 30.0f);
	layout.add(std::make_unique<juce::AudioParameterBool>(
	    juce::ParameterID { noteOffDamps, version }, "Note Off Damps", false));

	layout.add(std::make_unique<juce::AudioParameterChoice>(
	    juce::ParameterID { mode, version }, "Mode", modes.names(), 0, fixed()));
	layout.add(std::make_unique<juce::AudioParameterChoice>(
	    juce::ParameterID { kickModel, version }, "Kick Model", modelNames(Mode::kick), 0, fixed()));
	layout.add(std::make_unique<juce::AudioParameterChoice>(
	    juce::ParameterID { malletModel, version }, "Mallet Model", modelNames(Mode::mallet), 0, fixed()));

	addPercent(layout, kickSweep, "Kick Sweep", 0.0f);
	addPercent(layout, kickSweepTime, "Kick Sweep Time", 30.0f);
	addPercent(layout, kickClick, "Kick Click", 0.0f);
	addPercent(layout, kickBodyShape, "Kick Body Shape", 0.0f);

	addPercent(layout, barMaterial, "Bar Material", 20.0f);
	addPercent(layout, barHardness, "Bar Hardness", 50.0f);
	addPercent(layout, barPosition, "Bar Position", 25.0f);
	addPercent(layout, barOvertones, "Bar Overtones", 100.0f);
	addPercent(layout, barResonator, "Bar Resonator", 50.0f);

	// Tracking and Offline both default to Off, so a bounce matches playback (docs/FLINT_VALIDATION.md).
	layout.add(plugin_support::QualitySelection::makeTrackingParameter(trackingOversampling, version, 0));
	layout.add(plugin_support::QualitySelection::makeOfflineParameter(offlineOversampling, version, 0));
	return layout;
}
}
