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
	void drawRotarySlider(juce::Graphics&, int, int, int, int, float, float, float,
		juce::Slider&) override;
};
}
