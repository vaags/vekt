#include <vekt/ui/VektLookAndFeel.h>

namespace vekt::ui
{
VektLookAndFeel::VektLookAndFeel()
{
	setColour(juce::ResizableWindow::backgroundColourId, juce::Colour::fromRGB(16, 18, 20));
	setColour(juce::Slider::rotarySliderFillColourId, juce::Colour::fromRGB(227, 156, 75));
	setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour::fromRGB(66, 72, 76));
	setColour(juce::Slider::thumbColourId, juce::Colour::fromRGB(244, 228, 193));
	setColour(juce::ToggleButton::textColourId, juce::Colour::fromRGB(214, 218, 218));
	setColour(juce::ComboBox::backgroundColourId, juce::Colour::fromRGB(30, 34, 36));
	setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGB(72, 78, 80));
	setColour(juce::ComboBox::textColourId, juce::Colour::fromRGB(222, 224, 219));
}

void VektLookAndFeel::drawRotarySlider(juce::Graphics& graphics, int x, int y, int width,
	int height, float position, float startAngle, float endAngle, juce::Slider&)
{
	const auto drawableBounds = juce::Rectangle<float>(
		static_cast<float>(x), static_cast<float>(y),
		static_cast<float>(width), static_cast<float>(height));
	const auto dialSide = std::min(drawableBounds.getWidth(), drawableBounds.getHeight());
	const auto bounds = drawableBounds.withSizeKeepingCentre(dialSide, dialSide).reduced(8.0f);
	const auto radius = std::min(bounds.getWidth(), bounds.getHeight()) * 0.5f;
	const auto centre = bounds.getCentre();
	const auto dialBounds = bounds.reduced(8.0f);
	const auto dialRadius = std::min(dialBounds.getWidth(), dialBounds.getHeight()) * 0.5f;
	const auto needleThickness = juce::jlimit(2.5f, 5.0f, dialSide * 0.035f);
	graphics.setColour(juce::Colour::fromRGB(19, 24, 27));
	graphics.fillEllipse(dialBounds);
	graphics.setColour(juce::Colour::fromRGB(70, 82, 86));
	graphics.drawEllipse(dialBounds, 1.0f);
	graphics.setColour(findColour(juce::Slider::rotarySliderOutlineColourId));
	juce::Path track;
	track.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
	graphics.strokePath(track, juce::PathStrokeType(3.0f));
    graphics.setColour(findColour(juce::Slider::rotarySliderFillColourId));
	juce::Path arc;
	arc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, startAngle,
		startAngle + position * (endAngle - startAngle), true);
	graphics.strokePath(arc, juce::PathStrokeType(4.0f));
	const auto angle = startAngle + position * (endAngle - startAngle);
	graphics.setColour(juce::Colour::fromRGB(242, 239, 225));
	graphics.drawLine(centre.x, centre.y,
		centre.x + std::cos(angle - juce::MathConstants<float>::halfPi) * dialRadius * 0.88f,
		centre.y + std::sin(angle - juce::MathConstants<float>::halfPi) * dialRadius * 0.88f,
		needleThickness);
}
}
