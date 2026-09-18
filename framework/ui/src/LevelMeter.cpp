#include <vekt/ui/LevelMeter.h>

#include <juce_audio_basics/juce_audio_basics.h>

namespace vekt::ui
{
LevelMeter::LevelMeter(juce::String meterName, juce::Colour colour)
	: name(std::move(meterName)), meterColour(colour)
{
}

void LevelMeter::setLevel(float linearGain) noexcept
{
	level = juce::jlimit(0.0f, 1.0f,
		(juce::Decibels::gainToDecibels(linearGain, -60.0f) + 60.0f) / 60.0f);
}

void LevelMeter::paint(juce::Graphics& graphics)
{
	const auto bounds = getLocalBounds().toFloat();
	graphics.setColour(juce::Colour::fromRGB(16, 18, 20));
	graphics.fillRoundedRectangle(bounds, 2.0f);
	graphics.setColour(meterColour);
	graphics.fillRoundedRectangle(bounds.withWidth(bounds.getWidth() * level), 2.0f);
	graphics.setColour(juce::Colour::fromRGB(218, 220, 214));
	graphics.setFont(juce::FontOptions(10.0f).withStyle("Bold"));
	graphics.drawText(name, getLocalBounds().reduced(4, 0), juce::Justification::centredLeft);
}
}
