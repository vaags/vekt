#include <vekt/rav/Parameters.h>

#include <vekt/plugin_support/QualitySelection.h>

#include <memory>

namespace vekt::rav::parameters
{
namespace
{
constexpr auto parameterVersion = 1;

juce::NormalisableRange<float> decibelRange()
{
	return { -24.0f, 24.0f, 0.01f };
}
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
	juce::AudioProcessorValueTreeState::ParameterLayout layout;

	layout.add(std::make_unique<juce::AudioParameterFloat>(
		juce::ParameterID { inputGain, parameterVersion }, "Input", decibelRange(), 0.0f,
		juce::AudioParameterFloatAttributes {}.withLabel("dB")));
	layout.add(std::make_unique<juce::AudioParameterFloat>(
		juce::ParameterID { drive, parameterVersion }, "Drive",
		juce::NormalisableRange<float> { 0.0f, 36.0f, 0.01f }, 6.0f,
		juce::AudioParameterFloatAttributes {}.withLabel("dB")));
	layout.add(std::make_unique<juce::AudioParameterFloat>(
		juce::ParameterID { tone, parameterVersion }, "Tone",
		juce::NormalisableRange<float> { -6.0f, 6.0f, 0.01f }, 0.0f,
		juce::AudioParameterFloatAttributes {}.withLabel("dB/oct")));
	layout.add(std::make_unique<juce::AudioParameterFloat>(
		juce::ParameterID { bias, parameterVersion }, "Bias",
		juce::NormalisableRange<float> { -1.0f, 1.0f, 0.001f }, 0.0f));
	layout.add(std::make_unique<juce::AudioParameterBool>(
		juce::ParameterID { autoGain, parameterVersion }, "Auto Gain", false));
	layout.add(std::make_unique<juce::AudioParameterBool>(
		juce::ParameterID { bypass, parameterVersion }, "Bypass", false));
	layout.add(std::make_unique<juce::AudioParameterFloat>(
		juce::ParameterID { mix, parameterVersion }, "Mix",
		juce::NormalisableRange<float> { 0.0f, 100.0f, 0.01f }, 100.0f,
		juce::AudioParameterFloatAttributes {}.withLabel("%")));
	layout.add(std::make_unique<juce::AudioParameterFloat>(
		juce::ParameterID { outputGain, parameterVersion }, "Output", decibelRange(), 0.0f,
		juce::AudioParameterFloatAttributes {}.withLabel("dB")));
	layout.add(std::make_unique<juce::AudioParameterFloat>(
		juce::ParameterID { lowBandMix, parameterVersion }, "Low Mix",
		juce::NormalisableRange<float> { 0.0f, 100.0f, 0.01f }, 100.0f,
		juce::AudioParameterFloatAttributes {}.withLabel("%")));
	layout.add(std::make_unique<juce::AudioParameterFloat>(
		juce::ParameterID { midBandMix, parameterVersion }, "Mid Mix",
		juce::NormalisableRange<float> { 0.0f, 100.0f, 0.01f }, 100.0f,
		juce::AudioParameterFloatAttributes {}.withLabel("%")));
	layout.add(std::make_unique<juce::AudioParameterFloat>(
		juce::ParameterID { highBandMix, parameterVersion }, "High Mix",
		juce::NormalisableRange<float> { 0.0f, 100.0f, 0.01f }, 100.0f,
		juce::AudioParameterFloatAttributes {}.withLabel("%")));
	layout.add(std::make_unique<juce::AudioParameterFloat>(
		juce::ParameterID { lowMidCutoffHz, parameterVersion }, "Low-Mid Crossover",
		juce::NormalisableRange<float> { 40.0f, 2'000.0f, 0.01f, 0.35f }, 250.0f,
		juce::AudioParameterFloatAttributes {}.withLabel("Hz")));
	layout.add(std::make_unique<juce::AudioParameterFloat>(
		juce::ParameterID { midHighCutoffHz, parameterVersion }, "Mid-High Crossover",
		juce::NormalisableRange<float> { 500.0f, 16'000.0f, 0.01f, 0.35f }, 2'500.0f,
		juce::AudioParameterFloatAttributes {}.withLabel("Hz")));
	layout.add(std::make_unique<juce::AudioParameterChoice>(
		juce::ParameterID { mode, parameterVersion }, "Mode",
		juce::StringArray { "Saturation", "Overdrive", "Distortion", "Circuit Fuzz", "Gated Fuzz" }, 0));
	for (const auto& parameter : std::array {
		std::pair { shape, "Shape" }, std::pair { dynamics, "Dynamics" },
		std::pair { texture, "Texture" } })
		layout.add(std::make_unique<juce::AudioParameterFloat>(
			juce::ParameterID { parameter.first, parameterVersion }, parameter.second,
			juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 0.5f));
	for (std::size_t index = 0; index < stageEnabledIds.size(); ++index)
		layout.add(std::make_unique<juce::AudioParameterBool>(
			juce::ParameterID { stageEnabledIds[index], parameterVersion }, stageEnabledIds[index], index == 0));

	layout.add(plugin_support::QualitySelection::makeTrackingParameter(trackingOversampling, parameterVersion, 2));
	layout.add(plugin_support::QualitySelection::makeOfflineParameter(offlineOversampling, parameterVersion, 4));

    return layout;
}
}
