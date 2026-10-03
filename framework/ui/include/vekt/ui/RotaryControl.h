#pragma once

#include <vekt/ui/ModulationRing.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <optional>

namespace vekt::ui
{
class RotaryControl final : public juce::Component
{
public:
	enum class Size
	{
		compact,
		standard,
		large
	};

	RotaryControl();

	[[nodiscard]] static constexpr int heightFor(Size size) noexcept
	{
		switch (size)
		{
		case Size::compact: return 120;
		case Size::standard: return 140;
		case Size::large: return 160;
		}
		return 144;
	}

	void setLabel(const juce::String& text);
	void setLayout(Size dialSize, int valueWidth);
	void setWaveformGuide(bool shouldShow);
	// Fills the arc from the parameter's zero instead of its minimum, for signed amounts.
	void setBipolar(bool isBipolar);
	// For cyclic parameters: maps the range onto one full turn starting at startAngle (radians
	// clockwise from 12 o'clock) and draws a position cursor on a closed ring instead of a filled
	// amount. Dragging or scrolling past either end wraps round to the other end.
	void setEndless(bool isEndless, float startAngle = 0.0f);
	// Rounds values set by dragging, not by keys, text entry or the host: Flint's Pitch snaps to semitones unless Shift
	// is held. Empty (the default) leaves drags continuous.
	void setDragSnap(std::function<double(double)> snap);
	void refreshValueText();
	// Shows where modulation can take the value (empty hides it); the pointer and readout keep the base value.
	void setModulation(std::optional<ModulationDisplay> display);
	[[nodiscard]] const ModulationRing& getModulationRing() const noexcept { return modulationRing; }
	[[nodiscard]] juce::Slider& getSlider() noexcept;
	void resized() override;

private:
	class Dial final : public juce::Slider
	{
	public:
		std::function<double(double)> dragSnap;
		double snapValue(double attemptedValue, DragMode dragMode) override
		{
			return dragSnap && dragMode != notDragging ? dragSnap(attemptedValue) : attemptedValue;
		}
	};

	void updateValueText();

	Dial slider;
	ModulationRing modulationRing { slider };
	juce::Label valueLabel;
	juce::Label label;
	Size dialSize = Size::standard;
	int valueWidth = 96;
};
}
