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
	setStereoLevels({ linearGain, linearGain });
}

void LevelMeter::setStereoLevels(StereoMeterBallistics::Levels linearGains) noexcept
{
	ballistics.update(linearGains, StereoMeterBallistics::Clock::now());
	repaint();
}

void LevelMeter::paint(juce::Graphics& graphics)
{
	const auto bounds = getLocalBounds().toFloat();
	graphics.setColour(juce::Colour::fromRGB(16, 18, 20));
	graphics.fillRoundedRectangle(bounds, 2.0f);

	const auto drawLane = [&](juce::Rectangle<float> laneBounds, float level)
	{
		graphics.setColour(juce::Colour::fromRGB(27, 30, 33));
		graphics.fillRoundedRectangle(laneBounds, 1.0f);
		const auto displayLevel = juce::jlimit(0.0f, 1.0f,
			(juce::Decibels::gainToDecibels(level, -60.0f) + 60.0f) / 60.0f);
		if (displayLevel <= 0.0f)
			return;
		graphics.setColour(meterColour);
		const auto fillBounds = orientation == Orientation::horizontal
			? laneBounds.withWidth(laneBounds.getWidth() * displayLevel)
			: laneBounds.withTop(laneBounds.getBottom() - laneBounds.getHeight() * displayLevel);
		graphics.fillRoundedRectangle(fillBounds, 1.0f);
	};

	const auto& levels = ballistics.getDisplayedLevels();
	if (orientation == Orientation::vertical)
	{
		const auto labelHeight = 11.0f;
		const auto gap = 2.0f;
		const auto meterBounds = bounds.reduced(2.0f).withTrimmedBottom(labelHeight + 1.0f);
		const auto laneWidth = std::max(0.0f, (meterBounds.getWidth() - gap) / 2.0f);
		const auto leftLane = meterBounds.withWidth(laneWidth);
		const auto rightLane = leftLane.translated(laneWidth + gap, 0.0f);
		drawLane(leftLane, levels[0]);
		drawLane(rightLane, levels[1]);

		graphics.setColour(juce::Colour::fromRGB(218, 220, 214));
		graphics.setFont(juce::FontOptions(8.0f).withStyle("Bold"));
		const auto labelBounds = bounds.withTop(bounds.getBottom() - labelHeight);
		graphics.drawText("L", labelBounds.withWidth(bounds.getWidth() / 2.0f), juce::Justification::centred);
		graphics.drawText("R", labelBounds.withTrimmedLeft(bounds.getWidth() / 2.0f), juce::Justification::centred);
	}
	else
	{
		const auto gap = 2.0f;
		const auto laneHeight = std::max(0.0f, (bounds.getHeight() - gap) / 2.0f);
		drawLane(bounds.withHeight(laneHeight), levels[0]);
		drawLane(bounds.withTop(laneHeight + gap), levels[1]);
		graphics.setColour(juce::Colour::fromRGB(218, 220, 214));
		graphics.setFont(juce::FontOptions(14.0f).withStyle("Bold"));
		graphics.drawText(name, getLocalBounds().reduced(4, 0), juce::Justification::centredLeft);
	}
}
}
