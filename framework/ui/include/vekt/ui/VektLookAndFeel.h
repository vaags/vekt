#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace vekt::ui
{
class VektLookAndFeel final : public juce::LookAndFeel_V4
{
public:
	VektLookAndFeel();
	juce::Label* createSliderTextBox(juce::Slider&) override;
	juce::Slider::SliderLayout getSliderLayout(juce::Slider&) override;
	void drawCornerResizer(juce::Graphics&, int, int, bool, bool) override;
	// Sliders with the "bipolar" property fill from zero towards the value; sliders with the
	// "endless" property draw a closed ring with a position cursor instead of a filled amount. Sliders with the
	// "playableFrom" and "playableTo" properties (values in the slider's range) dim the ring outside that span: values
	// there are accepted but clamped by the sound (Flint's Pitch outside a model's range).
	void drawRotarySlider(juce::Graphics&, int, int, int, int, float, float, float,
		juce::Slider&) override;
	// Sliders with the "bipolar" property fill from their centre (zero) towards the value.
	void drawLinearSlider(juce::Graphics&, int, int, int, int, float, float, float,
		juce::Slider::SliderStyle, juce::Slider&) override;
};
}
