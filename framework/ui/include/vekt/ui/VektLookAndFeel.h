#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace vekt::ui
{
class VektLookAndFeel final : public juce::LookAndFeel_V4
{
public:
	VektLookAndFeel();
	void drawRotarySlider(juce::Graphics&, int, int, int, int, float, float, float,
		juce::Slider&) override;
};
}