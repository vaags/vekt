#include <vekt/ui/RotaryControl.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace
{
// Drags the dial straight up by the given distance (negative drags down) and returns the value.
double dragUp(juce::Slider& slider, double startValue, float pixels)
{
	slider.setValue(startValue, juce::dontSendNotification);
	const auto source = juce::Desktop::getInstance().getMainMouseSource();
	const auto start = slider.getLocalBounds().getCentre().toFloat();
	const auto time = juce::Time::getCurrentTime();
	const auto event = [&](juce::Point<float> position, bool dragged)
	{
		return juce::MouseEvent(source, position, juce::ModifierKeys::leftButtonModifier, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
			&slider, &slider, time, start, time, 1, dragged);
	};
	slider.mouseDown(event(start, false));
	slider.mouseDrag(event(start.translated(0.0f, -pixels), true));
	slider.mouseUp(event(start.translated(0.0f, -pixels), true));
	return slider.getValue();
}
}

TEST_CASE("Endless rotary controls wrap drags past either end", "[ui][rotary]")
{
	juce::ScopedJuceInitialiser_GUI initialiseJuce;
	vekt::ui::RotaryControl control;
	control.setBounds(0, 0, 100, vekt::ui::RotaryControl::heightFor(vekt::ui::RotaryControl::Size::standard));
	auto& slider = control.getSlider();
	slider.setRange(0.0, 360.0, 0.1);
	const auto fullDrag = static_cast<float>(slider.getMouseDragSensitivity());

	control.setEndless(true);
	REQUIRE(dragUp(slider, 350.0, fullDrag * 0.1f) == Catch::Approx(26.0));
	REQUIRE(dragUp(slider, 10.0, -fullDrag * 0.1f) == Catch::Approx(334.0));

	// A start angle turns the ring (so glyphs can sit on the diagonals) without changing the wrap.
	control.setEndless(true, juce::MathConstants<float>::pi * 1.25f);
	const auto rotary = slider.getRotaryParameters();
	REQUIRE(rotary.startAngleRadians == Catch::Approx(juce::MathConstants<float>::pi * 1.25f));
	REQUIRE(rotary.endAngleRadians == Catch::Approx(juce::MathConstants<float>::pi * 3.25f));
	REQUIRE_FALSE(rotary.stopAtEnd);
	REQUIRE(dragUp(slider, 350.0, fullDrag * 0.1f) == Catch::Approx(26.0));
	control.setEndless(true, -juce::MathConstants<float>::halfPi);
	REQUIRE(slider.getRotaryParameters().startAngleRadians == Catch::Approx(juce::MathConstants<float>::pi * 1.5f));

	control.setEndless(false);
	REQUIRE(dragUp(slider, 350.0, fullDrag * 0.1f) == Catch::Approx(360.0));
	REQUIRE(dragUp(slider, 10.0, -fullDrag * 0.1f) == Catch::Approx(0.0));
}
