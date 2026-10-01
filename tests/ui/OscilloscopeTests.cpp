#include <vekt/ui/Oscilloscope.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdlib>
#include <numbers>

TEST_CASE("Oscilloscope shows a triggered window of the tap's newest output", "[ui][scope]")
{
	juce::ScopedJuceInitialiser_GUI initialiseJuce;
	vekt::dsp::ScopeTap tap;
	tap.prepare(48'000.0);
	vekt::ui::Oscilloscope scope(tap, 0.01);
	scope.setSize(120, 80);
	scope.refresh();
	REQUIRE(scope.getShownLeft().size() == 480);

	// 220 Hz in the left channel, its inverse in the right.
	juce::AudioBuffer<float> buffer { 2, 4096 };
	for (auto sample = 0; sample < buffer.getNumSamples(); ++sample)
	{
		const auto value = static_cast<float>(0.7 * std::sin(2.0 * std::numbers::pi * 220.0 * sample / 48'000.0 + 0.4));
		buffer.setSample(0, sample, value);
		buffer.setSample(1, sample, -value * 0.5f);
	}
	tap.publish(buffer);
	scope.refresh();
	const auto left = scope.getShownLeft();
	const auto right = scope.getShownRight();
	REQUIRE(left.size() == 480);
	REQUIRE(right.size() == 480);
	// Starts at a rising zero crossing of the sum, which follows the left channel here.
	REQUIRE(std::abs(left.front()) < 0.05f);
	REQUIRE(left[1] > left.front());
	REQUIRE(juce::exactlyEqual(right.front(), -left.front() * 0.5f));
	// Paints without a display; the snapshot is opt-in, for visual review.
	juce::Image image(juce::Image::ARGB, 120, 80, true);
	juce::Graphics graphics(image);
	scope.paint(graphics);
	if (const auto* path = std::getenv("VEKT_SCOPE_SNAPSHOT"))
	{
		const juce::File file { juce::String(path) };
		file.deleteFile();
		juce::FileOutputStream stream { file };
		REQUIRE(juce::PNGImageFormat().writeImageToStream(scope.createComponentSnapshot(scope.getLocalBounds(), true, 4.0f), stream));
	}
}
