#include <vekt/ui/LevelMeter.h>

#include <catch2/catch_test_macros.hpp>

namespace
{
juce::Colour pixelFor(vekt::ui::LevelMeter& meter, int x, int y)
{
	meter.setBounds(0, 0, 28, 190);
	const auto image = meter.createComponentSnapshot(meter.getLocalBounds(), true, 1.0f);
	return image.getPixelAt(x, y);
}
}

TEST_CASE("LevelMeter renders stereo lanes independently", "[ui][meter]")
{
	juce::ScopedJuceInitialiser_GUI initialiseJuce;
	const auto colour = juce::Colour::fromRGB(91, 162, 150);

	vekt::ui::LevelMeter leftMeter { "IN", colour, vekt::ui::LevelMeter::Orientation::vertical };
	leftMeter.setStereoLevels({ 1.0f, 0.0f });
	const auto leftFill = pixelFor(leftMeter, 7, 20);
	const auto leftEmpty = pixelFor(leftMeter, 20, 20);
	REQUIRE(leftFill.getGreen() > leftEmpty.getGreen());

	vekt::ui::LevelMeter rightMeter { "IN", colour, vekt::ui::LevelMeter::Orientation::vertical };
	rightMeter.setStereoLevels({ 0.0f, 1.0f });
	const auto rightEmpty = pixelFor(rightMeter, 7, 20);
	const auto rightFill = pixelFor(rightMeter, 20, 20);
	REQUIRE(rightFill.getGreen() > rightEmpty.getGreen());

	vekt::ui::LevelMeter dualMonoMeter { "IN", colour, vekt::ui::LevelMeter::Orientation::vertical };
	dualMonoMeter.setLevel(1.0f);
	REQUIRE(pixelFor(dualMonoMeter, 7, 20).getGreen() > leftEmpty.getGreen());
	REQUIRE(pixelFor(dualMonoMeter, 20, 20).getGreen() > leftEmpty.getGreen());
}

TEST_CASE("LevelMeter turns only peaking input and output bars red", "[ui][meter]")
{
	juce::ScopedJuceInitialiser_GUI initialiseJuce;
	for (const auto& name : { "IN", "OUT" })
	{
		const auto colour = juce::Colour::fromRGB(227, 156, 75);
		vekt::ui::LevelMeter meter { name, colour, vekt::ui::LevelMeter::Orientation::vertical };
		meter.setStereoLevels({ 1.1f, 0.99f });
		const auto leftPeak = pixelFor(meter, 7, 60);
		const auto rightNormal = pixelFor(meter, 20, 60);
		REQUIRE(leftPeak.getRed() > leftPeak.getGreen() * 2);
		REQUIRE(rightNormal.getGreen() > rightNormal.getBlue());
		REQUIRE(rightNormal.getGreen() > leftPeak.getGreen());

		vekt::ui::LevelMeter rightMeter { name, colour, vekt::ui::LevelMeter::Orientation::vertical };
		rightMeter.setStereoLevels({ 0.99f, 1.0f });
		const auto leftNormal = pixelFor(rightMeter, 7, 60);
		const auto rightPeak = pixelFor(rightMeter, 20, 60);
		REQUIRE(leftNormal.getGreen() > rightPeak.getGreen());
		REQUIRE(rightPeak.getRed() > rightPeak.getGreen() * 2);
	}
}
