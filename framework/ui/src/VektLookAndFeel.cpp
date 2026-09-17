#include <vekt/ui/VektLookAndFeel.h>

namespace vekt::ui
{
VektLookAndFeel::VektLookAndFeel()
{
	setColour(juce::ResizableWindow::backgroundColourId, juce::Colour::fromRGB(20, 24, 28));
	setColour(juce::Slider::rotarySliderFillColourId, juce::Colour::fromRGB(89, 198, 178));
	setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour::fromRGB(54, 65, 70));
}

void VektLookAndFeel::drawRotarySlider(juce::Graphics& graphics, int x, int y, int width,
	int height, float position, float startAngle, float endAngle, juce::Slider&)
{
	const auto bounds = juce::Rectangle<float>(
		static_cast<float>(x), static_cast<float>(y),
		static_cast<float>(width), static_cast<float>(height)).reduced(5.0f);
	const auto radius = std::min(bounds.getWidth(), bounds.getHeight()) * 0.5f;
	const auto centre = bounds.getCentre();
	graphics.setColour(findColour(juce::Slider::rotarySliderOutlineColourId));
	graphics.drawEllipse(bounds, 2.0f);
	graphics.setColour(findColour(juce::Slider::rotarySliderFillColourId));
	juce::Path arc;
	arc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, startAngle,
		startAngle + position * (endAngle - startAngle), true);
	graphics.strokePath(arc, juce::PathStrokeType(2.5f));
	graphics.drawLine(centre.x, centre.y,
		centre.x + std::cos(startAngle + position * (endAngle - startAngle)) * radius * 0.65f,
		centre.y + std::sin(startAngle + position * (endAngle - startAngle)) * radius * 0.65f, 2.5f);
}
}