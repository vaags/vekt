#include <vekt/ui/ValueFormat.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Editor values have concise units and normalised zero", "[ui]")
{
	using namespace vekt::ui;
	REQUIRE(formatValue(-0.00001, ValueFormat::decibels) == "0.0 dB");
	REQUIRE(formatValue(6.0, ValueFormat::decibels) == "6.0 dB");
	REQUIRE(formatValue(0.0, ValueFormat::tone) == "0.0 dB/oct");
	REQUIRE(formatValue(0.5, ValueFormat::decimal) == "0.500");
	REQUIRE(formatValue(100.0, ValueFormat::percent) == "100%");
	REQUIRE(formatValue(32.5, ValueFormat::percent) == "32.5%");
	REQUIRE(formatValue(250.0, ValueFormat::frequency) == "250 Hz");
	REQUIRE(formatValue(2500.0, ValueFormat::frequency) == "2.50 kHz");
}

TEST_CASE("Editor numeric entry parses units without accepting invalid values", "[ui]")
{
	using namespace vekt::ui;
	REQUIRE(parseValue("2.5 kHz", ValueFormat::frequency).value() == 2500.0);
	REQUIRE(parseValue("250", ValueFormat::frequency).value() == 250.0);
	REQUIRE(parseValue(" 250 Hz ", ValueFormat::frequency).value() == 250.0);
	REQUIRE(parseValue("-3.2 dB", ValueFormat::decibels).value() == Catch::Approx(-3.2));
	REQUIRE(parseValue("1.5 dB/oct", ValueFormat::tone).value() == 1.5);
	REQUIRE(parseValue("32.5%", ValueFormat::percent).value() == 32.5);
	for (const auto* text : { "", "abc", "nan", "inf", "1e999", "1.2.3", "3 bananas", "3 dB" })
		REQUIRE_FALSE(parseValue(text, ValueFormat::frequency).has_value());
}

TEST_CASE("Rounded editor values retain precision on unchanged commits", "[ui]")
{
	juce::ScopedJuceInitialiser_GUI initialiseJuce;
	juce::Slider slider;
	slider.setRange(40.0, 16000.0, 0.01);
	slider.setValue(2501.23);
	vekt::ui::configureValueFormat(slider, vekt::ui::ValueFormat::frequency);
	REQUIRE(slider.getValueFromText(slider.getTextFromValue(slider.getValue())) == slider.getValue());
	REQUIRE(slider.getValueFromText("invalid") == slider.getValue());
	REQUIRE(static_cast<bool>(slider.getProperties()["valueEntryError"]));
	slider.setValue(slider.getValueFromText("99 kHz"));
	REQUIRE(slider.getValue() == 16000.0);
	REQUIRE_FALSE(static_cast<bool>(slider.getProperties()["valueEntryError"]));
}