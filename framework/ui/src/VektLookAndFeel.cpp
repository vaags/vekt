#include <vekt/ui/VektLookAndFeel.h>

namespace vekt::ui
{
namespace
{
void drawWaveformGlyph(juce::Graphics& graphics, juce::Point<float> centre, float size, int waveform)
{
	juce::Path path;
	const auto left = centre.x - size * 0.5f;
	const auto right = centre.x + size * 0.5f;
	const auto top = centre.y - size * 0.38f;
	const auto bottom = centre.y + size * 0.38f;
	switch (waveform)
	{
	case 0:
		path.startNewSubPath(left, centre.y);
		for (int point = 1; point <= 12; ++point)
		{
			const auto proportion = static_cast<float>(point) / 12.0f;
			path.lineTo(left + proportion * size, centre.y - std::sin(proportion * juce::MathConstants<float>::twoPi) * size * 0.38f);
		}
		break;
	case 1:
		path.startNewSubPath(left, bottom);
		path.lineTo(centre.x, top);
		path.lineTo(right, bottom);
		break;
	case 2:
		path.startNewSubPath(left, bottom);
		path.lineTo(right, top);
		break;
	case 3:
		path.startNewSubPath(left, bottom);
		path.lineTo(left, top);
		path.lineTo(centre.x, top);
		path.lineTo(centre.x, bottom);
		path.lineTo(right, bottom);
		break;
	default: break;
	}
	graphics.strokePath(path, juce::PathStrokeType(1.25f));
}
}

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

juce::Slider::SliderLayout VektLookAndFeel::getSliderLayout(juce::Slider& slider)
{
	if (!static_cast<bool>(slider.getProperties()["ioFader"]))
		return juce::LookAndFeel_V4::getSliderLayout(slider);
	juce::Slider::SliderLayout result;
	result.textBoxBounds = slider.getLocalBounds().removeFromBottom(24);
	result.sliderBounds = slider.getLocalBounds().withTrimmedTop(8).withTrimmedBottom(40);
	return result;
}

void VektLookAndFeel::drawCornerResizer(juce::Graphics& graphics, int width, int height,
	bool hovered, bool dragging)
{
	graphics.setColour(juce::Colour::fromRGB(116, 128, 132).withAlpha(hovered || dragging ? 1.0f : 0.6f));
	for (const auto offset : { 7.0f, 12.0f })
		graphics.drawLine(static_cast<float>(width) - offset, static_cast<float>(height) - 3.0f,
			static_cast<float>(width) - 3.0f, static_cast<float>(height) - offset, 1.5f);
}

juce::Label* VektLookAndFeel::createSliderTextBox(juce::Slider& slider)
{
	auto* textBox = juce::LookAndFeel_V4::createSliderTextBox(slider);
	auto valueFont = juce::Font(juce::FontOptions(static_cast<bool>(slider.getProperties()["ioFader"]) ? 12.0f : 14.0f));
	valueFont.setTypefaceName(juce::Font::getDefaultMonospacedFontName());
	textBox->setFont(valueFont);
	textBox->setJustificationType(juce::Justification::centred);
	return textBox;
}

void VektLookAndFeel::drawRotarySlider(juce::Graphics& graphics, int x, int y, int width,
	int height, float position, float startAngle, float endAngle, juce::Slider& slider)
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
	if (static_cast<bool>(slider.getProperties()["waveformGuide"]))
	{
		const auto glyphRadius = radius + juce::jlimit(7.0f, 13.0f, dialSide * 0.11f);
		const auto glyphSize = juce::jlimit(10.0f, 16.0f, dialSide * 0.16f);
		graphics.setColour(juce::Colour::fromRGB(170, 181, 180));
		for (int waveform = 0; waveform < 4; ++waveform)
		{
			const auto proportion = static_cast<float>(waveform) / 3.0f;
			const auto angle = startAngle + proportion * (endAngle - startAngle) - juce::MathConstants<float>::halfPi;
			drawWaveformGlyph(graphics, { centre.x + std::cos(angle) * glyphRadius,
				centre.y + std::sin(angle) * glyphRadius }, glyphSize, waveform);
		}
	}
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
