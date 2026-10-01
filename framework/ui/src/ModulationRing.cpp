#include <vekt/ui/ModulationRing.h>

#include <algorithm>
#include <cmath>

namespace vekt::ui
{
namespace
{
// The dot and its rim are 6 px across, as large as a compact knob's square holds at 3 and 9 o'clock (where only the
// rim's outer half pixel meets its edge); within that, a thin rim (still a whole pixel on Retina) leaves the most room
// for the dot itself.
constexpr auto dotDiameter = 5.0f;
constexpr auto dotRim = 0.5f;
// Shortest arc drawn, so a range squeezed against the end of the travel still shows.
constexpr auto minimumArc = 0.08f;
// The overflow mark: a fainter continuation of the arc into the dial's gap, after a short break.
constexpr auto overflowGap = 0.1f;
constexpr auto overflowLength = 0.18f;
// Fainter than the arc yet at least 3:1 against the panel (UI_UX.md), since the tail carries meaning: about 3.6:1.
constexpr auto overflowAlpha = 0.7f;
// The blur band is thicker than the arc, inside the knob's square, and not drawn while narrower than the dot.
constexpr auto blurStroke = 4.0f;
constexpr auto minimumBlur = 0.05f;
// Longest move, in radians, still treated as motion: a frame of the fastest the dot shows (a third of the 288-degree
// travel). Longer moves are jumps (a new voice, a reset) and get no afterglow.
constexpr auto maximumTrail = 1.7f;
// Shares of the knob's travel moved per drawn frame over which the dot hands over to the arc and blur band. Up to
// the start it stays full size and brightness, its afterglow filling most of the gap between frames; beyond, it would
// strobe into a row of dots. The handover is quick, since a fading dot reads as a shrinking one.
// JUCE draws at about 60 fps on macOS even on 120 Hz displays, so that is the frame rate assumed.
constexpr auto handoverStartTravel = 0.15;
constexpr auto handoverEndTravel = 0.25;
// The afterglow behind a moving dot: at most this many dot widths long, so it fills the gap between frames without
// growing into a sweep.
constexpr auto maximumAfterglowWidths = 5.0f;
constexpr auto assumedFrameRate = 60.0;
const auto defaultModulationColour = juce::Colour::fromRGB(160, 144, 236);
}

RotaryGeometry rotaryGeometry(juce::Rectangle<float> drawableBounds) noexcept
{
	const auto dialSide = std::min(drawableBounds.getWidth(), drawableBounds.getHeight());
	const auto bounds = drawableBounds.withSizeKeepingCentre(dialSide, dialSide).reduced(8.0f);
	return { bounds.getCentre(), std::min(bounds.getWidth(), bounds.getHeight()) * 0.5f, dialSide };
}

ModulationRing::ModulationRing(juce::Slider& controlled) : slider(controlled)
{
	setInterceptsMouseClicks(false, false);
	setVisible(false);
}

void ModulationRing::setModulation(std::optional<ModulationDisplay> display)
{
	if (display == modulation)
	{
		// The dot has stopped: drop its trail.
		if (trailStart)
		{
			trailStart.reset();
			repaint();
		}
		return;
	}
	modulation = display;
	setVisible(modulation.has_value());
	trailStart.reset();
	// A dot that disappears starts afresh when it comes back.
	if (!currentAngle()) paintedAngle.reset();
	// From where the dot was last drawn, not where the previous call put it: display callbacks can outnumber the
	// frames actually drawn (a 120 Hz display drawn at 60 fps), and the trail must cover all motion between frames.
	if (const auto now = currentAngle(); paintedAngle && now)
	{
		auto from = *paintedAngle;
		// Endless controls wrap: trail the short way round.
		if (isEndless())
			from = *now + std::remainder(from - *now, juce::MathConstants<float>::twoPi);
		// A long jump is a new voice or a reset, not motion; it gets no trail.
		if (std::abs(*now - from) <= maximumTrail) trailStart = from;
	}
	repaint();
}

bool ModulationRing::isEndless() const { return static_cast<bool>(slider.getProperties()["endless"]); }

ModulationRing::Overflow ModulationRing::overflow() const
{
	if (!modulation || isEndless()) return {};
	const auto low = std::min(modulation->lowest, modulation->highest);
	const auto high = std::max(modulation->lowest, modulation->highest);
	return { low < slider.getMinimum(), high > slider.getMaximum() };
}

bool ModulationRing::isCurrentBeyondTravel() const
{
	if (!currentAngle() || isEndless()) return false;
	return *modulation->current < slider.getMinimum() || *modulation->current > slider.getMaximum();
}

float ModulationRing::angleOf(double value) const
{
	const auto rotary = slider.getRotaryParameters();
	const auto start = rotary.startAngleRadians;
	const auto travel = rotary.endAngleRadians - start;
	const auto minimum = slider.getMinimum();
	const auto span = slider.getMaximum() - minimum;
	// Endless controls are linear and wrap: values past either end carry on round the ring.
	if (isEndless())
		return start + static_cast<float>((value - minimum) / span) * travel;
	// Bounded controls clamp, as the processor clamps the modulated value to the parameter's range.
	return start + static_cast<float>(slider.valueToProportionOfLength(std::clamp(value, minimum, minimum + span))) * travel;
}

std::optional<juce::Range<float>> ModulationRing::arcAngles() const
{
	if (!modulation || slider.getMaximum() <= slider.getMinimum()) return {};
	const auto rotary = slider.getRotaryParameters();
	const auto start = rotary.startAngleRadians;
	const auto travel = rotary.endAngleRadians - start;
	const auto low = std::min(modulation->lowest, modulation->highest);
	const auto high = std::max(modulation->lowest, modulation->highest);
	if (isEndless())
	{
		// A range of a whole turn or more covers the ring; anything shorter is drawn unwrapped from the low end (the
		// arc itself wraps past the start angle).
		if (high - low >= slider.getMaximum() - slider.getMinimum())
			return juce::Range<float>(start, start + juce::MathConstants<float>::twoPi);
		return juce::Range<float>(angleOf(low), angleOf(high));
	}
	auto from = angleOf(low), to = angleOf(high);
	if (to - from < minimumArc)
	{
		const auto first = std::min(start, start + travel), last = std::max(start, start + travel);
		const auto centre = std::clamp(0.5f * (from + to), first + 0.5f * minimumArc, last - 0.5f * minimumArc);
		from = centre - 0.5f * minimumArc;
		to = centre + 0.5f * minimumArc;
	}
	return juce::Range<float>(from, to);
}

float ModulationRing::dotOpacityForSpeed(double travelPerSecond) const
{
	const auto travelPerFrame = std::abs(travelPerSecond) / assumedFrameRate;
	return static_cast<float>(1.0 - std::clamp((travelPerFrame - handoverStartTravel) / (handoverEndTravel - handoverStartTravel), 0.0, 1.0));
}

std::optional<float> ModulationRing::currentAngle() const
{
	if (!modulation || !modulation->current || modulation->currentOpacity <= 0.0f || slider.getMaximum() <= slider.getMinimum())
		return {};
	return angleOf(*modulation->current);
}

std::optional<juce::Range<float>> ModulationRing::blurAngles() const
{
	if (!modulation || !modulation->blur || slider.getMaximum() <= slider.getMinimum()) return {};
	const auto low = std::min(modulation->blur->lowest, modulation->blur->highest);
	const auto high = std::max(modulation->blur->lowest, modulation->blur->highest);
	if (isEndless() && high - low >= slider.getMaximum() - slider.getMinimum())
		return juce::Range<float>(slider.getRotaryParameters().startAngleRadians,
			slider.getRotaryParameters().startAngleRadians + juce::MathConstants<float>::twoPi);
	return juce::Range<float>(angleOf(low), angleOf(high));
}

void ModulationRing::paint(juce::Graphics& graphics)
{
	const auto angles = arcAngles();
	paintedAngle = angles ? currentAngle() : std::nullopt;
	if (!angles) return;
	const auto geometry = rotaryGeometry(slider.getLookAndFeel().getSliderLayout(slider).sliderBounds.toFloat());
	const auto radius = geometry.modulationRadius();
	juce::Path arc;
	arc.addCentredArc(geometry.centre.x, geometry.centre.y, radius, radius, 0.0f, angles->getStart(), angles->getEnd(), true);
	const auto colour = isColourSpecified(modulationColourId) || getLookAndFeel().isColourSpecified(modulationColourId)
		? findColour(modulationColourId) : defaultModulationColour;
	const auto opacity = slider.isEnabled() ? 1.0f : 0.45f;
	const auto stroke = juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
	graphics.setColour(colour.withMultipliedAlpha(opacity));
	graphics.strokePath(arc, stroke);
	// Modulation the processor carries past the end of the knob's travel continues, faintly, into the dial's gap.
	const auto rotary = slider.getRotaryParameters();
	const auto direction = rotary.endAngleRadians >= rotary.startAngleRadians ? 1.0f : -1.0f;
	const auto drawTail = [&](float from, float towards)
	{
		juce::Path tail;
		const auto first = from + towards * overflowGap, last = from + towards * (overflowGap + overflowLength);
		tail.addCentredArc(geometry.centre.x, geometry.centre.y, radius, radius, 0.0f, std::min(first, last), std::max(first, last), true);
		graphics.setColour(colour.withMultipliedAlpha(overflowAlpha * opacity));
		graphics.strokePath(tail, stroke);
	};
	const auto overflowing = overflow();
	if (overflowing.below) drawTail(rotary.startAngleRadians, -direction);
	if (overflowing.above) drawTail(rotary.endAngleRadians, direction);
	// The blur band: a thicker, brighter stretch of the lane the live value is sweeping too fast to follow. It is
	// steady (its width follows depths and rates, not the output), so it moves with the dot without flickering.
	if (const auto band = blurAngles(); band && band->getLength() > minimumBlur)
	{
		juce::Path blur;
		blur.addCentredArc(geometry.centre.x, geometry.centre.y, radius, radius, 0.0f, band->getStart(), band->getEnd(), true);
		graphics.setColour(colour.interpolatedWith(juce::Colours::white, 0.25f).withMultipliedAlpha(0.85f * opacity));
		graphics.strokePath(blur, juce::PathStrokeType(blurStroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
	}
	if (const auto angle = paintedAngle)
	{
		// A lighter dot with a dark rim, so it stands out on the arc it travels along and reads apart from the
		// white pointer inside the dial. Past the end of the travel it is pinned there and drawn hollow.
		const auto dot = juce::Rectangle<float>(dotDiameter, dotDiameter)
			.withCentre(geometry.centre.getPointOnCircumference(radius, *angle));
		const auto dotOpacity = opacity * std::clamp(modulation->currentOpacity, 0.0f, 1.0f);
		const auto dotColour = colour.interpolatedWith(juce::Colours::white, 0.55f).withMultipliedAlpha(dotOpacity);
		// Afterglow: a dot-wide tail along the path since the frame last drawn, from the dot's brightness fading to
		// nothing, unrimmed. It fills most of the gap between frames, so fast motion does not strobe into a row of dots,
		// yet reads as a glow behind the dot rather than an object of its own (or the band, which is even and thick).
		if (trailStart && std::abs(*angle - *trailStart) > 0.01f && radius > 0.0f)
		{
			const auto motion = *angle > *trailStart ? 1.0f : -1.0f;
			const auto length = std::min(std::abs(*angle - *trailStart), maximumAfterglowWidths * dotDiameter / radius);
			const auto tailEnd = *angle - motion * length;
			juce::Path glow;
			glow.addCentredArc(geometry.centre.x, geometry.centre.y, radius, radius, 0.0f,
				std::min(tailEnd, *angle), std::max(tailEnd, *angle), true);
			graphics.setGradientFill(juce::ColourGradient(dotColour, geometry.centre.getPointOnCircumference(radius, *angle),
				dotColour.withAlpha(0.0f), geometry.centre.getPointOnCircumference(radius, tailEnd), false));
			graphics.strokePath(glow, juce::PathStrokeType(dotDiameter, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
		}
		graphics.setColour(juce::Colour::fromRGB(20, 24, 28).withMultipliedAlpha(dotOpacity));
		graphics.fillEllipse(dot.expanded(dotRim));
		graphics.setColour(dotColour);
		if (isCurrentBeyondTravel())
			graphics.drawEllipse(dot.reduced(0.6f), 1.2f);
		else
			graphics.fillEllipse(dot);
	}
}
}
