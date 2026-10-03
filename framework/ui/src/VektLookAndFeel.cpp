#include <vekt/ui/VektLookAndFeel.h>

#include <vekt/ui/ModulationRing.h>

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
		path.lineTo(right, bottom);
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
	graphics.strokePath(path, juce::PathStrokeType(1.0f));
}
}

VektLookAndFeel::VektLookAndFeel()
{
	setColour(juce::ResizableWindow::backgroundColourId, juce::Colour::fromRGB(16, 18, 20));
	setColour(juce::Slider::rotarySliderFillColourId, juce::Colour::fromRGB(227, 156, 75));
	setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour::fromRGB(66, 72, 76));
	setColour(juce::Slider::thumbColourId, juce::Colour::fromRGB(244, 228, 193));
	// Modulation ranges: distinct from the orange accent and the filter group's teal.
	setColour(ModulationRing::modulationColourId, juce::Colour::fromRGB(160, 144, 236));
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
	const auto waveformGuide = static_cast<bool>(slider.getProperties()["waveformGuide"]);
	// Knob geometry depends only on the RotaryControl size. Waveform guides use the
	// spare corners of the square slider canvas and must not make the knob smaller.
	const auto geometry = rotaryGeometry(drawableBounds);
	const auto dialSide = geometry.dialSide;
	const auto radius = geometry.radius;
	const auto centre = geometry.centre;
	const auto bounds = juce::Rectangle<float>(2.0f * radius, 2.0f * radius).withCentre(centre);
	const auto bipolar = static_cast<bool>(slider.getProperties()["bipolar"]);
	const auto endless = static_cast<bool>(slider.getProperties()["endless"]);
	const auto opacity = slider.isEnabled() ? 1.0f : 0.45f;
	const auto ringStroke = juce::PathStrokeType(4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
	const auto angle = startAngle + position * (endAngle - startAngle);
	const auto strokeSpan = [&](float from, float to, juce::Colour colour)
	{
		juce::Path arc;
		arc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, std::min(from, to), std::max(from, to), true);
		graphics.setColour(colour.withMultipliedAlpha(opacity));
		graphics.strokePath(arc, ringStroke);
	};
	const auto& properties = slider.getProperties();
	const auto limited = !endless && properties.contains("playableFrom") && properties.contains("playableTo");
	const auto angleOf = [&](double value)
	{
		return startAngle + static_cast<float>(juce::jlimit(0.0, 1.0, slider.valueToProportionOfLength(value)))
			* (endAngle - startAngle);
	};
	const auto playableStart = limited ? angleOf(static_cast<double>(properties["playableFrom"])) : startAngle;
	const auto playableEnd = limited ? angleOf(static_cast<double>(properties["playableTo"])) : endAngle;
	// Parts of an arc outside the playable span are drawn at a third of their strength.
	const auto strokeArc = [&](float from, float to, juce::Colour colour)
	{
		const auto low = std::min(from, to), high = std::max(from, to);
		if (!limited)
		{
			strokeSpan(low, high, colour);
			return;
		}
		const auto dimmed = colour.withMultipliedAlpha(0.35f);
		if (low < playableStart) strokeSpan(low, std::min(high, playableStart), dimmed);
		if (high > playableStart && low < playableEnd)
			strokeSpan(std::max(low, playableStart), std::min(high, playableEnd), colour);
		if (high > playableEnd) strokeSpan(std::max(low, playableEnd), high, dimmed);
	};
	// The ring sits on the rim of the dial: one object instead of a dial floating inside a track.
	// The dial is a step lighter than Panel so it reads as a raised knob, including in the ring's gap.
	graphics.setColour(juce::Colour::fromRGB(38, 45, 49).withMultipliedAlpha(opacity));
	graphics.fillEllipse(bounds);
	const auto trackColour = findColour(juce::Slider::rotarySliderOutlineColourId);
	// Endless controls close the ring, since they have no start or end to leave a gap for.
	if (endless)
		strokeArc(0.0f, juce::MathConstants<float>::twoPi, trackColour);
	else
		strokeArc(startAngle, endAngle, trackColour);
	if (waveformGuide)
	{
		// Keep the annotations visually subordinate to the dial. A large, bright ring
		// of glyphs makes an identically-sized guided knob appear smaller by contrast.
		const auto glyphSize = juce::jlimit(8.0f, 10.0f, dialSide * 0.10f);
		const auto glyphRadius = radius + 14.0f;
		graphics.setColour(juce::Colour::fromRGB(150, 162, 162).withMultipliedAlpha(opacity));
		for (int waveform = 0; waveform < 4; ++waveform)
		{
			// Glyph n marks the value n (0 sine to 3 square), wherever the slider's range and angles put it.
			// The glyphs sit outside the dial and are only visible near the diagonals, in the square
			// canvas's corners.
			const auto proportion = static_cast<float>(slider.valueToProportionOfLength(waveform));
			const auto glyphAngle = startAngle + proportion * (endAngle - startAngle) - juce::MathConstants<float>::halfPi;
			drawWaveformGlyph(graphics, { centre.x + std::cos(glyphAngle) * glyphRadius,
				centre.y + std::sin(glyphAngle) * glyphRadius }, glyphSize, waveform);
		}
	}
	// A slider may override the active arc without changing its dial, needle or value readout.
	const auto fillColour = slider.findColour(juce::Slider::rotarySliderFillColourId);
	const auto pointOnRadius = [&](float distance, float atAngle)
	{
		return centre.getPointOnCircumference(distance, atAngle);
	};
	if (endless)
	{
		// A short cursor marks position without implying an amount filled from a start point.
		constexpr auto cursorHalfWidth = juce::degreesToRadians(14.0f);
		strokeArc(angle - cursorHalfWidth, angle + cursorHalfWidth, fillColour);
	}
	else if (bipolar)
	{
		const auto zeroProportion = static_cast<float>(juce::jlimit(0.0, 1.0, slider.valueToProportionOfLength(0.0)));
		const auto zeroAngle = startAngle + zeroProportion * (endAngle - startAngle);
		if (std::abs(angle - zeroAngle) > 0.01f)
			strokeArc(zeroAngle, angle, fillColour);
		// The tick crosses the ring, over the fill, rather than standing outside it: outside is the modulation lane.
		graphics.setColour(juce::Colour::fromRGB(116, 128, 132).withMultipliedAlpha(opacity));
		graphics.drawLine(juce::Line<float>(pointOnRadius(radius - 3.0f, zeroAngle), pointOnRadius(radius + 4.0f, zeroAngle)), 1.5f);
	}
	else if (position > 0.0f)
	{
		strokeArc(startAngle, angle, fillColour);
	}
	// The pointer stops just inside the ring so its tip lands where the value arc ends.
	juce::Path pointer;
	pointer.startNewSubPath(pointOnRadius(radius * 0.3f, angle));
	pointer.lineTo(pointOnRadius(radius - 7.0f, angle));
	graphics.setColour(juce::Colour::fromRGB(242, 239, 225).withMultipliedAlpha(opacity));
	graphics.strokePath(pointer, juce::PathStrokeType(juce::jlimit(2.5f, 5.0f, dialSide * 0.035f),
		juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void VektLookAndFeel::drawLinearSlider(juce::Graphics& graphics, int x, int y, int width, int height,
	float sliderPosition, float minimumPosition, float maximumPosition, juce::Slider::SliderStyle style, juce::Slider& slider)
{
	if (!static_cast<bool>(slider.getProperties()["bipolar"]) || style != juce::Slider::LinearHorizontal)
	{
		juce::LookAndFeel_V4::drawLinearSlider(graphics, x, y, width, height, sliderPosition, minimumPosition, maximumPosition, style, slider);
		return;
	}
	const auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height));
	const auto track = bounds.withSizeKeepingCentre(bounds.getWidth(), 6.0f);
	const auto centre = static_cast<float>(slider.getPositionOfValue(0.0));
	const auto active = !juce::approximatelyEqual(slider.getValue(), 0.0);
	graphics.setColour(juce::Colour::fromRGB(19, 24, 27));
	graphics.fillRoundedRectangle(track, 3.0f);
	graphics.setColour(juce::Colour::fromRGB(70, 82, 86));
	graphics.drawRoundedRectangle(track, 3.0f, 1.0f);
	graphics.setColour(juce::Colour::fromRGB(116, 128, 132));
	graphics.fillRect(juce::Rectangle<float>(centre - 0.5f, track.getY() - 3.0f, 1.0f, track.getHeight() + 6.0f));
	if (active)
	{
		graphics.setColour(findColour(juce::Slider::rotarySliderFillColourId).withMultipliedAlpha(slider.isEnabled() ? 1.0f : 0.5f));
		graphics.fillRect(juce::Rectangle<float>::leftTopRightBottom(std::min(centre, sliderPosition), track.getY() + 1.0f,
			std::max(centre, sliderPosition), track.getBottom() - 1.0f));
	}
	graphics.setColour(active ? findColour(juce::Slider::thumbColourId) : juce::Colour::fromRGB(150, 162, 162));
	graphics.fillRoundedRectangle(juce::Rectangle<float>(sliderPosition - 2.5f, bounds.getCentreY() - 7.0f, 5.0f, 14.0f), 2.0f);
}
}
