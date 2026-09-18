#include <vekt/ui/RotaryControl.h>

namespace vekt::ui
{
namespace
{
constexpr int labelHeight = 20;
constexpr int valueHeight = 22;
}

RotaryControl::RotaryControl()
{
	slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
	slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 100, valueHeight);
	label.setJustificationType(juce::Justification::centred);
	addAndMakeVisible(slider);
	addAndMakeVisible(label);
}

void RotaryControl::setLabel(juce::String text)
{
	slider.setName(text);
	slider.setTooltip(text);
	label.setText(std::move(text), juce::dontSendNotification);
}

juce::Slider& RotaryControl::getSlider() noexcept
{
	return slider;
}

void RotaryControl::resized()
{
	const auto bounds = getLocalBounds();
	const auto dialSide = juce::jmin(bounds.getWidth(), bounds.getHeight() - labelHeight);
	const auto dialBounds = bounds.withHeight(dialSide).withSizeKeepingCentre(dialSide, dialSide);
	slider.setBounds(dialBounds);
	label.setBounds(bounds.withY(dialBounds.getBottom()).withHeight(labelHeight));
}
}
