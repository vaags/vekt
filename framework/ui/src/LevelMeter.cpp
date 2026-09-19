#include <vekt/ui/LevelMeter.h>

#include <juce_audio_basics/juce_audio_basics.h>

namespace vekt::ui
{
LevelMeter::LevelMeter(juce::String meterName, juce::Colour colour, Orientation meterOrientation)
	: name(std::move(meterName)), meterColour(colour), orientation(meterOrientation)
{
	setName(name);
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
	if (orientation == Orientation::vertical)
	{
		graphics.setFont(juce::FontOptions(9.0f));
		for (const auto decibels : { 0, -12, -24, -48 })
		{
			const auto y = bounds.getHeight() * static_cast<float>(-decibels) / 60.0f;
			graphics.drawHorizontalLine(juce::roundToInt(y), 0.0f, 4.0f);
			graphics.drawText(juce::String(decibels), 5, juce::jlimit(0, getHeight() - 12,
				juce::roundToInt(y) - 6), getWidth() - 6, 12, juce::Justification::centredRight);
		}
	}
	else
	{
		graphics.setFont(juce::FontOptions(14.0f).withStyle("Bold"));
		graphics.drawText(name, getLocalBounds().reduced(4, 0), juce::Justification::centredLeft);
	}
}
}
