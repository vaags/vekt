#include "Parameters.h"

#include <memory>

namespace vekt::saturator::parameters
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

	const auto qualityAttributes = juce::AudioParameterChoiceAttributes {}.withAutomatable(false);
	layout.add(std::make_unique<juce::AudioParameterChoice>(
		juce::ParameterID { oversamplingFactor, parameterVersion }, "Oversampling",
		juce::StringArray { "Off", "2x", "4x" }, 2, qualityAttributes));
	layout.add(std::make_unique<juce::AudioParameterChoice>(
		juce::ParameterID { oversamplingPhase, parameterVersion }, "Filter Phase",
		juce::StringArray { "Minimum Phase", "Linear Phase" }, 0, qualityAttributes));

	return layout;
}

dsp::OversamplingQuality qualityFrom(float factorIndex, float phaseIndex) noexcept
{
	const auto factor = [factorIndex]
	{
		switch (juce::roundToInt(factorIndex))
		{
			case 0: return dsp::OversamplingFactor::off;
			case 1: return dsp::OversamplingFactor::x2;
			default: return dsp::OversamplingFactor::x4;
		}
	}();

	const auto phase = juce::roundToInt(phaseIndex) == 1
		? dsp::OversamplingPhase::linear
		: dsp::OversamplingPhase::minimum;

	return { factor, phase };
}
}
