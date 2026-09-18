#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace vekt::ui
{
class RotaryControl final : public juce::Component
{
public:
	RotaryControl();

	void setLabel(juce::String text);
	[[nodiscard]] juce::Slider& getSlider() noexcept;
	void resized() override;

private:
	juce::Slider slider;
	juce::Label label;
};
}
