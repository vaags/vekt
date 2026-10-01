#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

namespace vekt::ui
{
// Where modulation can take a control, in the control's own parameter units. The product converts its modulation
// depths to these units and limits them to what is audible (the processor's own limits, which may be wider than the
// knob's range). The ring maps them through the slider's range and skew. On a bounded control, a range reaching past
// either end of the travel is drawn to that end with an overflow mark beyond it; on an endless control it wraps.
struct ModulationDisplay
{
	double lowest {}, highest {};
	// The live modulated value, drawn as a dot on the arc; empty hides the dot (nothing sounding).
	std::optional<double> current;
	// Dot opacity, for fading it out where it would move too fast to follow.
	float currentOpacity { 1.0f };
	// Where the live value is moving too fast for the dot to follow, drawn as a brighter band on the arc (a motion
	// blur) around the dot. The value heard lies within it. Empty when the dot follows everything or nothing sounds.
	struct Band
	{
		double lowest {}, highest {};
	};
	std::optional<Band> blur;

	[[nodiscard]] bool operator==(const ModulationDisplay& other) const noexcept
	{
		const auto sameBand = [](const std::optional<Band>& first, const std::optional<Band>& second)
		{
			return first.has_value() == second.has_value()
				&& (!first || (juce::exactlyEqual(first->lowest, second->lowest) && juce::exactlyEqual(first->highest, second->highest)));
		};
		return juce::exactlyEqual(lowest, other.lowest) && juce::exactlyEqual(highest, other.highest)
			&& current.has_value() == other.current.has_value()
			&& (!current || juce::exactlyEqual(*current, *other.current))
			&& juce::exactlyEqual(currentOpacity, other.currentOpacity)
			&& sameBand(blur, other.blur);
	}
};

// Dial geometry of a rotary slider's drawable area, shared by VektLookAndFeel and overlays so they line up.
struct RotaryGeometry
{
	juce::Point<float> centre;
	float radius {};
	float dialSide {};
	// Radius of the modulation lane, between the value ring and the edge of the slider's square.
	// Kept inside the square with room for the live dot at 3 and 9 o'clock on compact knobs.
	[[nodiscard]] float modulationRadius() const noexcept { return radius + 5.5f; }
};
[[nodiscard]] RotaryGeometry rotaryGeometry(juce::Rectangle<float> drawableBounds) noexcept;

// Draws a control's modulation range as a thin arc in its own lane outside the value ring, so the pointer and value
// readout keep showing the base value. Sits over the slider and ignores the mouse.
class ModulationRing final : public juce::Component
{
public:
	enum ColourIds
	{
		modulationColourId = 0x7e0a0001
	};

	explicit ModulationRing(juce::Slider& slider);

	// Empty hides the ring. Repaints only when something drawn changes. Called once per display frame while the
	// value moves: behind the dot, a short fading afterglow covers its path since it was last drawn, so fast motion
	// reads as one moving dot rather than a row of separate dots, however often frames are actually drawn.
	void setModulation(std::optional<ModulationDisplay> display);
	[[nodiscard]] const std::optional<ModulationDisplay>& getModulation() const noexcept { return modulation; }
	// The arc's angles (radians clockwise from 12 o'clock, as JUCE rotary angles) for the current range; empty when hidden.
	[[nodiscard]] std::optional<juce::Range<float>> arcAngles() const;
	// The live dot's angle, mapped like the arc; empty when there is no dot or it is fully faded.
	[[nodiscard]] std::optional<float> currentAngle() const;
	// Which ends of a bounded control's travel the range reaches past. Endless controls never overflow.
	struct Overflow
	{
		bool below {}, above {};
	};
	[[nodiscard]] Overflow overflow() const;
	// Whether the live value is past the end of a bounded control's travel: the dot is pinned there, drawn hollow.
	[[nodiscard]] bool isCurrentBeyondTravel() const;
	// The blur band's angles, mapped like the arc; empty when there is no band.
	[[nodiscard]] std::optional<juce::Range<float>> blurAngles() const;
	// How visible a dot moving at this peak speed (a share of the knob's travel per second) should be: fully, at full
	// size, while it moves up to 15 % of the travel per drawn frame (its afterglow filling most of the gaps); then it
	// hands over quickly to the arc and blur band, gone by 25 %, beyond which it would strobe into a row of dots.
	[[nodiscard]] float dotOpacityForSpeed(double travelPerSecond) const;
	// Where the dot was last drawn, which its afterglow trails back towards; empty when it did not move continuously.
	[[nodiscard]] std::optional<float> trailStartAngle() const noexcept { return trailStart; }
	void paint(juce::Graphics&) override;

private:
	// The angle of a value: clamped to the travel on bounded controls, unwrapped from the minimum on endless ones.
	[[nodiscard]] float angleOf(double value) const;
	[[nodiscard]] bool isEndless() const;

	juce::Slider& slider;
	std::optional<ModulationDisplay> modulation;
	std::optional<float> trailStart, paintedAngle;
};
}
