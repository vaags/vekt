#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace vekt::ui
{
class RotaryControl final : public juce::Component
{
public:
	enum class Size
	{
		compact,
		standard,
		large
	};

	RotaryControl();

	[[nodiscard]] static constexpr int heightFor(Size size) noexcept
	{
		switch (size)
		{
		case Size::compact: return 120;
		case Size::standard: return 144;
		case Size::large: return 168;
		}
		return 144;
	}

	void setLabel(juce::String text);
	[[nodiscard]] juce::Slider& getSlider() noexcept;
	void resized() override;

private:
	juce::Slider slider;
	juce::Label label;
};
}
