#include <vekt/mono/Parameters.h>

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
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
	juce::AudioProcessorValueTreeState::ParameterLayout layout;
	layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { voiceCount, version }, "Voice Count", juce::StringArray { "8", "12", "16" }, 0, nonAutomatable()));
	layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { performanceMode, version }, "Performance Mode", juce::StringArray { "Poly", "Mono", "Mono Legato" }, 0));
	layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { quality, version }, "Quality", juce::StringArray { "Real-time", "High" }, 0, nonAutomatable()));
	layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { unison, version }, "Unison", juce::StringArray { "1x", "2x", "4x" }, 0));
	for (const auto [identifier, name, maximum] : { std::tuple { unisonDetune, "Unison Detune", 50.0f }, std::tuple { unisonSpread, "Unison Spread", 100.0f }, std::tuple { voiceWidth, "Voice Pan", 100.0f }, std::tuple { drift, "Drift", 100.0f } })
		layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { identifier, version }, name, juce::NormalisableRange<float> { 0.0f, maximum, 0.01f }, 0.0f, juce::AudioParameterFloatAttributes {}.withLabel("%")));
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
	return layout;
}
}
