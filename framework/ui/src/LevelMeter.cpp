#include <vekt/ui/LevelMeter.h>

#include <juce_audio_basics/juce_audio_basics.h>

namespace vekt::ui
{
LevelMeter::LevelMeter(juce::String meterName, juce::Colour colour, Orientation meterOrientation)
	: name(std::move(meterName)), meterColour(colour), orientation(meterOrientation)
{
}

void LevelMeter::setLevel(float linearGain) noexcept
{
	level = juce::jlimit(0.0f, 1.0f,
		(juce::Decibels::gainToDecibels(linearGain, -60.0f) + 60.0f) / 60.0f);
	repaint();
}

void LevelMeter::paint(juce::Graphics& graphics)
{
	const auto bounds = getLocalBounds().toFloat();
	graphics.setColour(juce::Colour::fromRGB(16, 18, 20));
	graphics.fillRoundedRectangle(bounds, 2.0f);
	if (level > 0.0f)
	{
		graphics.setColour(meterColour);
		const auto fillBounds = orientation == Orientation::horizontal
			? bounds.withWidth(bounds.getWidth() * level)
			: bounds.withTop(bounds.getBottom() - bounds.getHeight() * level);
		graphics.fillRoundedRectangle(fillBounds, 2.0f);
	}
	graphics.setColour(juce::Colour::fromRGB(218, 220, 214));
	graphics.setFont(juce::FontOptions(14.0f).withStyle("Bold"));
	graphics.drawText(name, getLocalBounds().reduced(4, 0),
		orientation == Orientation::horizontal ? juce::Justification::centredLeft : juce::Justification::centred);
}
}
