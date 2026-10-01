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

vekt::ui::ModulationDisplay range(double lowest, double highest)
{
	vekt::ui::ModulationDisplay display;
	display.lowest = lowest;
	display.highest = highest;
	return display;
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

TEST_CASE("Modulation rings map ranges through the slider and hide when empty", "[ui][rotary][modulation]")
{
	juce::ScopedJuceInitialiser_GUI initialiseJuce;
	vekt::ui::RotaryControl control;
	control.setBounds(0, 0, 100, vekt::ui::RotaryControl::heightFor(vekt::ui::RotaryControl::Size::standard));
	auto& slider = control.getSlider();
	slider.setRange(0.0, 100.0, 0.01);
	slider.setValue(50.0, juce::dontSendNotification);
	const auto& ring = control.getModulationRing();
	REQUIRE_FALSE(ring.isVisible());
	REQUIRE_FALSE(ring.arcAngles().has_value());
	// The ring overlays the dial exactly and never takes the dial's mouse input.
	REQUIRE(ring.getBounds() == slider.getBounds());
	bool interceptsSelf {}, interceptsChildren {};
	ring.getInterceptsMouseClicks(interceptsSelf, interceptsChildren);
	REQUIRE_FALSE(interceptsSelf);

	const auto rotary = slider.getRotaryParameters();
	const auto angleAt = [&](float proportion)
	{
		return rotary.startAngleRadians + proportion * (rotary.endAngleRadians - rotary.startAngleRadians);
	};
	control.setModulation(range(60.0, 40.0));
	REQUIRE(ring.isVisible());
	REQUIRE(ring.arcAngles()->getStart() == Catch::Approx(angleAt(0.4f)));
	REQUIRE(ring.arcAngles()->getEnd() == Catch::Approx(angleAt(0.6f)));

	// Bounded controls clamp to their range, as the processor clamps the modulated value.
	control.setModulation(range(-20.0, 30.0));
	REQUIRE(ring.arcAngles()->getStart() == Catch::Approx(angleAt(0.0f)));
	REQUIRE(ring.arcAngles()->getEnd() == Catch::Approx(angleAt(0.3f)));
	// A range pushed entirely past the end still shows, as a short arc inside the travel.
	control.setModulation(range(120.0, 150.0));
	REQUIRE(ring.arcAngles()->getEnd() == Catch::Approx(angleAt(1.0f)));
	REQUIRE(ring.arcAngles()->getLength() > 0.05f);

	// Skewed ranges map through the slider's own skew.
	slider.setNormalisableRange({ 5.0, 20'000.0, 0.01, 0.25 });
	control.setModulation(range(500.0, 2'000.0));
	REQUIRE(ring.arcAngles()->getStart() == Catch::Approx(angleAt(static_cast<float>(slider.valueToProportionOfLength(500.0)))));
	REQUIRE(ring.arcAngles()->getEnd() == Catch::Approx(angleAt(static_cast<float>(slider.valueToProportionOfLength(2'000.0)))));

	control.setModulation({});
	REQUIRE_FALSE(ring.isVisible());
}

TEST_CASE("Endless modulation rings wrap and fill the ring at a full turn", "[ui][rotary][modulation]")
{
	juce::ScopedJuceInitialiser_GUI initialiseJuce;
	vekt::ui::RotaryControl control;
	control.setBounds(0, 0, 100, vekt::ui::RotaryControl::heightFor(vekt::ui::RotaryControl::Size::standard));
	auto& slider = control.getSlider();
	slider.setRange(0.0, 4.0, 0.001);
	control.setEndless(true, juce::MathConstants<float>::pi * 1.25f);
	const auto start = slider.getRotaryParameters().startAngleRadians;
	const auto& ring = control.getModulationRing();

	// Past the end it carries on round the ring instead of clamping.
	control.setModulation(range(3.5, 4.5));
	REQUIRE(ring.arcAngles()->getStart() == Catch::Approx(start + 0.875f * juce::MathConstants<float>::twoPi));
	REQUIRE(ring.arcAngles()->getEnd() == Catch::Approx(start + 1.125f * juce::MathConstants<float>::twoPi));
	control.setModulation(range(0.0, 5.0));
	REQUIRE(ring.arcAngles()->getLength() == Catch::Approx(juce::MathConstants<float>::twoPi));
}

TEST_CASE("Modulation dots map the live value like the arc and hide without one", "[ui][rotary][modulation]")
{
	juce::ScopedJuceInitialiser_GUI initialiseJuce;
	vekt::ui::RotaryControl control;
	control.setBounds(0, 0, 100, vekt::ui::RotaryControl::heightFor(vekt::ui::RotaryControl::Size::standard));
	auto& slider = control.getSlider();
	slider.setRange(0.0, 100.0, 0.01);
	const auto& ring = control.getModulationRing();
	const auto rotary = slider.getRotaryParameters();
	const auto angleAt = [&](float proportion)
	{
		return rotary.startAngleRadians + proportion * (rotary.endAngleRadians - rotary.startAngleRadians);
	};
	vekt::ui::ModulationDisplay display;
	display.lowest = 40.0;
	display.highest = 60.0;
	control.setModulation(display);
	// No live value (nothing sounding): the arc shows without a dot.
	REQUIRE(ring.arcAngles().has_value());
	REQUIRE_FALSE(ring.currentAngle().has_value());

	display.current = 55.0;
	control.setModulation(display);
	REQUIRE(*ring.currentAngle() == Catch::Approx(angleAt(0.55f)));
	// Clamped like the arc on bounded controls.
	display.lowest = 80.0;
	display.highest = 120.0;
	display.current = 110.0;
	control.setModulation(display);
	REQUIRE(*ring.currentAngle() == Catch::Approx(angleAt(1.0f)));
	// A fully faded dot is not drawn.
	display.currentOpacity = 0.0f;
	control.setModulation(display);
	REQUIRE_FALSE(ring.currentAngle().has_value());

	// Endless controls carry the dot on round the ring, where the unwrapped arc ends.
	slider.setRange(0.0, 4.0, 0.001);
	control.setEndless(true, juce::MathConstants<float>::pi * 1.25f);
	display.lowest = 3.5;
	display.highest = 4.5;
	display.current = 4.25;
	display.currentOpacity = 1.0f;
	control.setModulation(display);
	const auto start = slider.getRotaryParameters().startAngleRadians;
	REQUIRE(*ring.currentAngle() == Catch::Approx(start + 4.25f / 4.0f * juce::MathConstants<float>::twoPi));
	REQUIRE(*ring.currentAngle() <= ring.arcAngles()->getEnd());
}

TEST_CASE("Modulation rings mark ranges and live values past the travel", "[ui][rotary][modulation]")
{
	juce::ScopedJuceInitialiser_GUI initialiseJuce;
	vekt::ui::RotaryControl control;
	control.setBounds(0, 0, 100, vekt::ui::RotaryControl::heightFor(vekt::ui::RotaryControl::Size::standard));
	auto& slider = control.getSlider();
	slider.setRange(0.0, 100.0, 0.01);
	const auto& ring = control.getModulationRing();
	REQUIRE_FALSE(ring.overflow().below);
	REQUIRE_FALSE(ring.overflow().above);

	auto display = range(80.0, 130.0);
	control.setModulation(display);
	REQUIRE_FALSE(ring.overflow().below);
	REQUIRE(ring.overflow().above);
	// A live value inside the travel is an ordinary dot; past it, the dot is pinned at the end and drawn hollow.
	display.current = 95.0;
	control.setModulation(display);
	REQUIRE_FALSE(ring.isCurrentBeyondTravel());
	display.current = 120.0;
	control.setModulation(display);
	REQUIRE(ring.isCurrentBeyondTravel());
	REQUIRE(*ring.currentAngle() == Catch::Approx(slider.getRotaryParameters().endAngleRadians));

	display = range(-10.0, 10.0);
	control.setModulation(display);
	REQUIRE(ring.overflow().below);
	REQUIRE_FALSE(ring.overflow().above);
	// A range that only reaches the ends exactly does not overflow.
	control.setModulation(range(0.0, 100.0));
	REQUIRE_FALSE(ring.overflow().below);
	REQUIRE_FALSE(ring.overflow().above);

	// Endless controls wrap instead of overflowing.
	slider.setRange(0.0, 4.0, 0.001);
	control.setEndless(true);
	display = range(3.0, 5.0);
	display.current = 4.5;
	control.setModulation(display);
	REQUIRE_FALSE(ring.overflow().above);
	REQUIRE_FALSE(ring.isCurrentBeyondTravel());
}

TEST_CASE("Modulation blur bands map like the arc", "[ui][rotary][modulation]")
{
	juce::ScopedJuceInitialiser_GUI initialiseJuce;
	vekt::ui::RotaryControl control;
	control.setBounds(0, 0, 100, vekt::ui::RotaryControl::heightFor(vekt::ui::RotaryControl::Size::standard));
	auto& slider = control.getSlider();
	slider.setRange(0.0, 100.0, 0.01);
	const auto& ring = control.getModulationRing();
	const auto rotary = slider.getRotaryParameters();
	const auto angleAt = [&](float proportion)
	{
		return rotary.startAngleRadians + proportion * (rotary.endAngleRadians - rotary.startAngleRadians);
	};
	auto display = range(20.0, 80.0);
	control.setModulation(display);
	REQUIRE_FALSE(ring.blurAngles().has_value());
	display.blur = vekt::ui::ModulationDisplay::Band { 30.0, 50.0 };
	control.setModulation(display);
	REQUIRE(ring.blurAngles()->getStart() == Catch::Approx(angleAt(0.3f)));
	REQUIRE(ring.blurAngles()->getEnd() == Catch::Approx(angleAt(0.5f)));
	// A band changing alone still repaints: it is part of the display's identity.
	auto moved = display;
	moved.blur = vekt::ui::ModulationDisplay::Band { 35.0, 55.0 };
	REQUIRE_FALSE(moved == display);
	// Clamped to bounded travel.
	display.blur = vekt::ui::ModulationDisplay::Band { 90.0, 130.0 };
	control.setModulation(display);
	REQUIRE(ring.blurAngles()->getStart() == Catch::Approx(angleAt(0.9f)));
	REQUIRE(ring.blurAngles()->getEnd() == Catch::Approx(angleAt(1.0f)));
	// A band of a full turn or more covers an endless ring.
	slider.setRange(0.0, 4.0, 0.001);
	control.setEndless(true);
	display = range(0.0, 4.0);
	display.blur = vekt::ui::ModulationDisplay::Band { -1.0, 4.0 };
	control.setModulation(display);
	REQUIRE(ring.blurAngles()->getLength() == Catch::Approx(juce::MathConstants<float>::twoPi));
}

TEST_CASE("Modulation dots trail their motion since the frame last drawn", "[ui][rotary][modulation]")
{
	juce::ScopedJuceInitialiser_GUI initialiseJuce;
	vekt::ui::RotaryControl control;
	control.setBounds(0, 0, 100, vekt::ui::RotaryControl::heightFor(vekt::ui::RotaryControl::Size::standard));
	auto& slider = control.getSlider();
	slider.setRange(0.0, 100.0, 0.01);
	const auto& ring = control.getModulationRing();
	// What the display does when it draws a frame.
	const auto draw = [&] { static_cast<void>(control.createComponentSnapshot(control.getLocalBounds())); };
	auto display = range(0.0, 100.0);
	display.current = 50.0;
	control.setModulation(display);
	// The first position has nothing to trail from.
	REQUIRE_FALSE(ring.trailStartAngle().has_value());
	draw();
	const auto drawn = *ring.currentAngle();
	// Two display callbacks before the next frame is drawn (120 Hz callbacks, 60 fps drawing): the trail still starts
	// where the dot was drawn, covering all the motion since.
	display.current = 55.0;
	control.setModulation(display);
	display.current = 60.0;
	control.setModulation(display);
	REQUIRE(*ring.trailStartAngle() == Catch::Approx(drawn));
	draw();
	// A frame without motion drops the trail; so does a jump (a new voice), and so does losing the dot.
	control.setModulation(display);
	REQUIRE_FALSE(ring.trailStartAngle().has_value());
	display.current = 5.0;
	control.setModulation(display);
	REQUIRE_FALSE(ring.trailStartAngle().has_value());
	draw();
	display.current.reset();
	control.setModulation(display);
	REQUIRE_FALSE(ring.trailStartAngle().has_value());
	// Coming back, the dot starts afresh rather than trailing from where it vanished.
	display.current = 6.0;
	control.setModulation(display);
	REQUIRE_FALSE(ring.trailStartAngle().has_value());

	// Endless controls trail the short way across the wrap.
	slider.setRange(0.0, 4.0, 0.001);
	control.setEndless(true);
	display = range(0.0, 4.0);
	display.current = 3.95;
	control.setModulation(display);
	draw();
	display.current = 0.05;
	control.setModulation(display);
	REQUIRE(ring.trailStartAngle().has_value());
	REQUIRE(*ring.currentAngle() - *ring.trailStartAngle() == Catch::Approx(0.1f / 4.0f * juce::MathConstants<float>::twoPi));
}

TEST_CASE("Modulation dots hand over to the band only when moving too fast to follow", "[ui][rotary][modulation]")
{
	juce::ScopedJuceInitialiser_GUI initialiseJuce;
	vekt::ui::RotaryControl compact, large;
	compact.setLayout(vekt::ui::RotaryControl::Size::compact, 62);
	compact.setBounds(0, 0, 65, vekt::ui::RotaryControl::heightFor(vekt::ui::RotaryControl::Size::compact));
	large.setLayout(vekt::ui::RotaryControl::Size::large, 96);
	large.setBounds(0, 0, 160, vekt::ui::RotaryControl::heightFor(vekt::ui::RotaryControl::Size::large));
	const auto& ring = compact.getModulationRing();
	// Full up to 15 % of the travel per 60 fps frame (9 travels a second), gone by 25 % (15).
	REQUIRE(ring.dotOpacityForSpeed(0.0) == Catch::Approx(1.0f));
	REQUIRE(ring.dotOpacityForSpeed(9.0) == Catch::Approx(1.0f));
	REQUIRE(ring.dotOpacityForSpeed(12.0) == Catch::Approx(0.5f));
	REQUIRE(ring.dotOpacityForSpeed(15.0) == Catch::Approx(0.0f));
	REQUIRE(ring.dotOpacityForSpeed(-12.0) == Catch::Approx(0.5f));
	// A share of the travel looks the same on any size of knob.
	REQUIRE(large.getModulationRing().dotOpacityForSpeed(12.0) == Catch::Approx(ring.dotOpacityForSpeed(12.0)));
}
