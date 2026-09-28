#include <vekt/mono/Parameters.h>

#include "Lfo.h"

#include <memory>

namespace vekt::mono::parameters
{
namespace
{
constexpr auto version = 1;

juce::AudioParameterChoiceAttributes nonAutomatable()
{
	return juce::AudioParameterChoiceAttributes {}.withAutomatable(false);
}


void addOscillator(juce::AudioProcessorValueTreeState::ParameterLayout& layout, int index,
	const char* range, const char* semitone, const char* fine, const char* level, const char* morph, const char* width)
{
	layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { range, version }, "Osc " + juce::String(index) + " Range",
		juce::StringArray { "16'", "8'", "4'", "2'", "1'" }, 1));
	layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID { semitone, version }, "Osc " + juce::String(index) + " Semitone", -24, 24, 0));
	layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { fine, version }, "Osc " + juce::String(index) + " Fine",
		juce::NormalisableRange<float> { -100.0f, 100.0f, 0.01f }, 0.0f, juce::AudioParameterFloatAttributes {}.withLabel("ct")));
	layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { level, version }, "Osc " + juce::String(index) + " Level",
		juce::NormalisableRange<float> { 0.0f, 100.0f, 0.01f }, index == 1 ? 100.0f : 0.0f, juce::AudioParameterFloatAttributes {}.withLabel("%")));
	layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { morph, version }, "Osc " + juce::String(index) + " Morph",
		juce::NormalisableRange<float> { 0.0f, 3.0f, 0.001f }, 2.0f));
	layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { width, version }, "Osc " + juce::String(index) + " Pulse Width",
		juce::NormalisableRange<float> { 5.0f, 95.0f, 0.01f }, 50.0f, juce::AudioParameterFloatAttributes {}.withLabel("%")));
}

// Continuous LFO ranges would otherwise print every float digit ("2.0000005").
juce::AudioParameterFloatAttributes withDecimals(int decimals, const char* label)
{
	return juce::AudioParameterFloatAttributes {}.withLabel(label)
		.withStringFromValueFunction([decimals](float value, int) { return juce::String(value, decimals); });
}

// Depths are bipolar; a symmetric skew keeps small vibrato and sweep amounts easy to set.
void addDepth(juce::AudioProcessorValueTreeState::ParameterLayout& layout, const char* identifier, const juce::String& name,
	float maximum, float skew, const char* label)
{
	layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { identifier, version }, name,
		juce::NormalisableRange<float> { -maximum, maximum, 0.0f, skew, true }, 0.0f, withDecimals(2, label)));
}

void addLfo(juce::AudioProcessorValueTreeState::ParameterLayout& layout, int number, const LfoParameterIds& ids)
{
	const auto prefix = "LFO " + juce::String(number) + " ";
	juce::NormalisableRange<float> rateRange { minimumLfoRateHz, maximumLfoRateHz };
	rateRange.setSkewForCentre(1.0f);
	layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { ids.rate, version }, prefix + "Rate", rateRange, 2.0f,
		withDecimals(2, "Hz")));
	layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { ids.sync, version }, prefix + "Sync", false));
	juce::StringArray divisions;
	for (const auto& [division, beats] : lfoDivisions) { juce::ignoreUnused(beats); divisions.add(division); }
	layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { ids.division, version }, prefix + "Division", divisions, defaultLfoDivision));
	layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { ids.shape, version }, prefix + "Shape",
		juce::StringArray { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Smooth Random" }, 0));
	layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { ids.polarity, version }, prefix + "Polarity", juce::StringArray { "Bipolar", "Unipolar" }, 0));
	layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { ids.mode, version }, prefix + "Mode", juce::StringArray { "Free", "Retrigger", "One Shot" }, 0));
	layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { ids.phase, version }, prefix + "Phase",
		juce::NormalisableRange<float> { 0.0f, 360.0f, 0.1f }, 0.0f, withDecimals(1, "deg")));
	for (const auto [identifier, name] : { std::pair { ids.delay, "Delay" }, std::pair { ids.fade, "Fade" } })
		layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { identifier, version }, prefix + name,
			juce::NormalisableRange<float> { 0.0f, 10.0f, 0.0001f, 0.35f }, 0.0f, withDecimals(3, "s")));
	// Amount defaults to full so turning up any one destination is immediately audible; all depths default to zero.
	layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { ids.amount, version }, prefix + "Amount",
		juce::NormalisableRange<float> { 0.0f, 100.0f, 0.01f }, 100.0f, withDecimals(1, "%")));
	for (std::size_t oscillator = 0; oscillator < 3; ++oscillator)
	{
		const auto target = prefix + "Osc " + juce::String(static_cast<int>(oscillator) + 1) + " ";
		addDepth(layout, ids.pitch[oscillator], target + "Pitch", 24.0f, 0.35f, "st");
		addDepth(layout, ids.morph[oscillator], target + "Morph", 100.0f, 1.0f, "%");
		addDepth(layout, ids.width[oscillator], target + "Width", 100.0f, 1.0f, "%");
		addDepth(layout, ids.level[oscillator], target + "Level", 100.0f, 1.0f, "%");
	}
	addDepth(layout, ids.filter, prefix + "Filter", 6.0f, 0.5f, "oct");
	addDepth(layout, ids.amp, prefix + "Amp", 100.0f, 1.0f, "%");
	addDepth(layout, ids.drive, prefix + "Drive", 24.0f, 1.0f, "dB");
	addDepth(layout, ids.noise, prefix + "Noise", 100.0f, 1.0f, "%");
	addDepth(layout, ids.detune, prefix + "Detune", 100.0f, 1.0f, "%");
	addDepth(layout, ids.spread, prefix + "Spread", 100.0f, 1.0f, "%");
}
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
	juce::AudioProcessorValueTreeState::ParameterLayout layout;
	layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { voiceCount, version }, "Voice Count", juce::StringArray { "2", "4", "8", "12", "16" }, 2, nonAutomatable()));
	layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { performanceMode, version }, "Performance Mode", juce::StringArray { "Poly", "Mono", "Mono Legato" }, 0));
	layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { quality, version }, "Quality", juce::StringArray { "1x", "2x", "4x", "8x" }, 0, nonAutomatable()));
	layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { unison, version }, "Unison", juce::StringArray { "1x", "2x", "4x" }, 0));
	// Unison Detune is in cents and defaults to 15 so switching unison on thickens the sound straight away.
	layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { unisonDetune, version }, "Unison Detune",
		juce::NormalisableRange<float> { 0.0f, 50.0f, 0.01f }, 15.0f, juce::AudioParameterFloatAttributes {}.withLabel("ct")));
	for (const auto [identifier, name] : { std::pair { unisonSpread, "Unison Spread" }, std::pair { voiceWidth, "Voice Pan" }, std::pair { drift, "Drift" } })
		layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { identifier, version }, name, juce::NormalisableRange<float> { 0.0f, 100.0f, 0.01f }, 0.0f, juce::AudioParameterFloatAttributes {}.withLabel("%")));
	layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { glideMode, version }, "Glide", juce::StringArray { "Off", "Always", "Legato" }, 0));
	layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { glideTime, version }, "Glide Time", juce::NormalisableRange<float> { 0.0f, 5.0f, 0.001f, 0.35f }, 0.0f, juce::AudioParameterFloatAttributes {}.withLabel("s")));
	layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID { pitchBendRange, version }, "Pitch Bend Range", 1, 24, 2, juce::AudioParameterIntAttributes {}.withLabel("st")));
	layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { calibration, version }, "Calibration", juce::NormalisableRange<float> { -100.0f, 100.0f, 0.01f }, 0.0f, juce::AudioParameterFloatAttributes {}.withLabel("ct")));
	layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { masterOutput, version }, "Master Output", juce::NormalisableRange<float> { -24.0f, 12.0f, 0.01f }, 0.0f, juce::AudioParameterFloatAttributes {}.withLabel("dB")));
	addOscillator(layout, 1, osc1Range, osc1Semitone, osc1Fine, osc1Level, osc1Morph, osc1PulseWidth);
	addOscillator(layout, 2, osc2Range, osc2Semitone, osc2Fine, osc2Level, osc2Morph, osc2PulseWidth);
	addOscillator(layout, 3, osc3Range, osc3Semitone, osc3Fine, osc3Level, osc3Morph, osc3PulseWidth);
	layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { noiseType, version }, "Noise", juce::StringArray { "Off", "White", "Pink" }, 0));
	layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { noiseLevel, version }, "Noise Level", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.01f }, 0.0f, juce::AudioParameterFloatAttributes {}.withLabel("%")));
	layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { filterCutoff, version }, "Cutoff", juce::NormalisableRange<float> { 20.0f, 20'000.0f, 0.01f, 0.25f }, 1'000.0f, juce::AudioParameterFloatAttributes {}.withLabel("Hz")));
	layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { filterResonance, version }, "Resonance", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.01f }, 10.0f, juce::AudioParameterFloatAttributes {}.withLabel("%")));
	layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { filterKeyTracking, version }, "Key Tracking", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.01f }, 50.0f, juce::AudioParameterFloatAttributes {}.withLabel("%")));
	layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { filterEnvelopeAmount, version }, "Filter Envelope", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.01f }, 50.0f, juce::AudioParameterFloatAttributes {}.withLabel("%")));
	layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { filterDrive, version }, "Filter Drive", juce::NormalisableRange<float> { 0.0f, 24.0f, 0.01f }, 0.0f, juce::AudioParameterFloatAttributes {}.withLabel("dB")));
	for (const auto [identifier, name, start, end, initial] : { std::tuple { ampAttack, "Amp Attack", 0.0005f, 10.0f, 0.005f }, std::tuple { ampDecay, "Amp Decay", 0.005f, 20.0f, 0.25f }, std::tuple { ampRelease, "Amp Release", 0.005f, 20.0f, 0.3f }, std::tuple { filterAttack, "Filter Attack", 0.0005f, 10.0f, 0.005f }, std::tuple { filterDecay, "Filter Decay", 0.005f, 20.0f, 0.5f }, std::tuple { filterRelease, "Filter Release", 0.005f, 20.0f, 0.4f } })
		layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { identifier, version }, name, juce::NormalisableRange<float> { start, end, 0.0001f, 0.35f }, initial, juce::AudioParameterFloatAttributes {}.withLabel("s")));
	layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { ampSustain, version }, "Amp Sustain", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.01f }, 75.0f, juce::AudioParameterFloatAttributes {}.withLabel("%")));
	layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { filterSustain, version }, "Filter Sustain", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.01f }, 25.0f, juce::AudioParameterFloatAttributes {}.withLabel("%")));
	layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { ampVelocity, version }, "Amp Velocity", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.01f }, 50.0f, juce::AudioParameterFloatAttributes {}.withLabel("%")));
	layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { filterVelocity, version }, "Filter Velocity", juce::NormalisableRange<float> { 0.0f, 100.0f, 0.01f }, 50.0f, juce::AudioParameterFloatAttributes {}.withLabel("%")));
	// Append new parameters to preserve the registration indices of existing host automation.
	for (const auto [identifier, name] : { std::pair { osc1Octave, "Osc 1 Octave" }, std::pair { osc2Octave, "Osc 2 Octave" }, std::pair { osc3Octave, "Osc 3 Octave" } })
		layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID { identifier, version }, name, -2, 2, 0,
			juce::AudioParameterIntAttributes {}.withLabel("oct")));
	layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { heldKeyReturn, version }, "Held Key Return", true));
	layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { filterQCompensation, version }, "Q Compensation", false));
	layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { notePriority, version }, "Mono Priority", juce::StringArray { "Last", "Low" }, 0));
	for (std::size_t index = 0; index < lfos.size(); ++index) addLfo(layout, static_cast<int>(index) + 1, lfos[index]);
	juce::NormalisableRange<float> vibratoRateRange { 0.1f, 12.0f };
	vibratoRateRange.setSkewForCentre(4.0f);
	layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { vibratoRate, version }, "Vibrato Rate", vibratoRateRange, 5.5f, withDecimals(2, "Hz")));
	layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { vibratoShape, version }, "Vibrato Shape", juce::StringArray { "Sine", "Triangle" }, 0));
	// Depth is reached with the mod wheel or aftertouch fully up; at rest the vibrato is silent.
	layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { vibratoDepth, version }, "Vibrato Depth",
		juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 50.0f, withDecimals(1, "ct")));
	return layout;
}
}
