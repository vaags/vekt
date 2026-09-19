#include <vekt/glimmer/Parameters.h>

#include <memory>

namespace vekt::glimmer::parameters
{
namespace
{
constexpr auto parameterVersion = 1;

juce::NormalisableRange<float> decibelRange()
{
	return { -24.0f, 24.0f, 0.01f };
}

juce::AudioParameterChoiceAttributes qualityAttributes()
{
	return juce::AudioParameterChoiceAttributes {}.withAutomatable(false);
}
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
	juce::AudioProcessorValueTreeState::ParameterLayout layout;

	layout.add(std::make_unique<juce::AudioParameterFloat>(
		juce::ParameterID { inputGain, parameterVersion }, "Input", decibelRange(), 0.0f,
		juce::AudioParameterFloatAttributes {}.withLabel("dB")));
	layout.add(std::make_unique<juce::AudioParameterFloat>(
		juce::ParameterID { preampDrive, parameterVersion }, "Preamp Drive",
		juce::NormalisableRange<float> { 0.0f, 36.0f, 0.01f }, 0.0f,
		juce::AudioParameterFloatAttributes {}.withLabel("dB")));
	layout.add(std::make_unique<juce::AudioParameterFloat>(
		juce::ParameterID { hornDrumBalance, parameterVersion }, "Horn / Drum Balance",
		juce::NormalisableRange<float> { -100.0f, 100.0f, 0.01f }, 0.0f,
		juce::AudioParameterFloatAttributes {}.withLabel("%")));
	layout.add(std::make_unique<juce::AudioParameterFloat>(
		juce::ParameterID { micAngle, parameterVersion }, "Mic Angle",
		juce::NormalisableRange<float> { 0.0f, 360.0f, 0.01f }, 0.0f,
		juce::AudioParameterFloatAttributes {}.withLabel("deg")));
	layout.add(std::make_unique<juce::AudioParameterFloat>(
		juce::ParameterID { micDistance, parameterVersion }, "Mic Distance",
		juce::NormalisableRange<float> { 0.3f, 3.0f, 0.001f }, 1.0f,
		juce::AudioParameterFloatAttributes {}.withLabel("m")));
	layout.add(std::make_unique<juce::AudioParameterFloat>(
		juce::ParameterID { slowSpeed, parameterVersion }, "Slow Speed",
		juce::NormalisableRange<float> { 20.0f, 120.0f, 0.01f }, 48.0f,
		juce::AudioParameterFloatAttributes {}.withLabel("rpm")));
	layout.add(std::make_unique<juce::AudioParameterFloat>(
		juce::ParameterID { fastSpeed, parameterVersion }, "Fast Speed",
		juce::NormalisableRange<float> { 180.0f, 600.0f, 0.01f }, 400.0f,
		juce::AudioParameterFloatAttributes {}.withLabel("rpm")));
	layout.add(std::make_unique<juce::AudioParameterFloat>(
		juce::ParameterID { accelerationTime, parameterVersion }, "Acceleration",
		juce::NormalisableRange<float> { 0.1f, 15.0f, 0.01f, 0.4f }, 2.0f,
		juce::AudioParameterFloatAttributes {}.withLabel("s")));
	layout.add(std::make_unique<juce::AudioParameterFloat>(
		juce::ParameterID { decelerationTime, parameterVersion }, "Deceleration",
		juce::NormalisableRange<float> { 0.1f, 20.0f, 0.01f, 0.4f }, 4.0f,
		juce::AudioParameterFloatAttributes {}.withLabel("s")));
	layout.add(std::make_unique<juce::AudioParameterFloat>(
		juce::ParameterID { hornTone, parameterVersion }, "Horn Tone",
		juce::NormalisableRange<float> { -12.0f, 12.0f, 0.01f }, 0.0f,
		juce::AudioParameterFloatAttributes {}.withLabel("dB")));
	layout.add(std::make_unique<juce::AudioParameterFloat>(
		juce::ParameterID { drumTone, parameterVersion }, "Drum Tone",
		juce::NormalisableRange<float> { -12.0f, 12.0f, 0.01f }, 0.0f,
		juce::AudioParameterFloatAttributes {}.withLabel("dB")));
	layout.add(std::make_unique<juce::AudioParameterChoice>(
		juce::ParameterID { speedMode, parameterVersion }, "Speed",
		juce::StringArray { "Slow", "Fast", "Auto" }, 0));
	layout.add(std::make_unique<juce::AudioParameterFloat>(
		juce::ParameterID { sensitivity, parameterVersion }, "Sensitivity",
		juce::NormalisableRange<float> { 0.0f, 100.0f, 0.01f }, 50.0f,
		juce::AudioParameterFloatAttributes {}.withLabel("%")));
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
	layout.add(std::make_unique<juce::AudioParameterChoice>(
		juce::ParameterID { trackingOversampling, parameterVersion }, "Tracking Oversampling",
		juce::StringArray { "Off", "2x IIR", "4x IIR", "2x FIR", "4x FIR", "8x FIR", "16x FIR" },
		2, qualityAttributes()));
	layout.add(std::make_unique<juce::AudioParameterChoice>(
		juce::ParameterID { offlineOversampling, parameterVersion }, "Offline Oversampling",
		juce::StringArray { "Off", "2x FIR", "4x FIR", "8x FIR", "16x FIR", "2x IIR", "4x IIR" },
		4, qualityAttributes()));

	layout.add(std::make_unique<juce::AudioParameterChoice>(
		juce::ParameterID { cabinetModel, 2 }, "Model", juce::StringArray { "Classic", "Drum", "Wide" }, 0));
	layout.add(std::make_unique<juce::AudioParameterBool>(
		juce::ParameterID { brake, 2 }, "Brake", false));
	layout.add(std::make_unique<juce::AudioParameterFloat>(
		juce::ParameterID { stereoWidth, 2 }, "Width",
		juce::NormalisableRange<float> { 0.0f, 200.0f, 0.01f }, 100.0f,
		juce::AudioParameterFloatAttributes {}.withLabel("%")));
	layout.add(std::make_unique<juce::AudioParameterBool>(
		juce::ParameterID { manualSpeedEnabled, 2 }, "Manual Speed", false));
	layout.add(std::make_unique<juce::AudioParameterFloat>(
		juce::ParameterID { speedPosition, 2 }, "Speed Position",
		juce::NormalisableRange<float> { 0.0f, 100.0f, 0.01f }, 0.0f,
		juce::AudioParameterFloatAttributes {}.withLabel("%")));

	return layout;
}

dsp::OversamplingQuality trackingQualityFrom(float index) noexcept
{
	switch (juce::roundToInt(index))
	{
	case 0: return { dsp::OversamplingFactor::off, dsp::OversamplingFilter::polyphaseIIR };
	case 1: return { dsp::OversamplingFactor::x2, dsp::OversamplingFilter::polyphaseIIR };
	case 2: return { dsp::OversamplingFactor::x4, dsp::OversamplingFilter::polyphaseIIR };
	case 3: return { dsp::OversamplingFactor::x2, dsp::OversamplingFilter::polyphaseFIR };
	case 4: return { dsp::OversamplingFactor::x4, dsp::OversamplingFilter::polyphaseFIR };
	case 5: return { dsp::OversamplingFactor::x8, dsp::OversamplingFilter::polyphaseFIR };
	default: return { dsp::OversamplingFactor::x16, dsp::OversamplingFilter::polyphaseFIR };
	}
}

dsp::OversamplingQuality offlineQualityFrom(float index) noexcept
{
	switch (juce::roundToInt(index))
	{
	case 0: return { dsp::OversamplingFactor::off, dsp::OversamplingFilter::polyphaseIIR };
	case 1: return { dsp::OversamplingFactor::x2, dsp::OversamplingFilter::polyphaseFIR };
	case 2: return { dsp::OversamplingFactor::x4, dsp::OversamplingFilter::polyphaseFIR };
	case 3: return { dsp::OversamplingFactor::x8, dsp::OversamplingFilter::polyphaseFIR };
	case 4: return { dsp::OversamplingFactor::x16, dsp::OversamplingFilter::polyphaseFIR };
	case 5: return { dsp::OversamplingFactor::x2, dsp::OversamplingFilter::polyphaseIIR };
	default: return { dsp::OversamplingFactor::x4, dsp::OversamplingFilter::polyphaseIIR };
	}
}
}
