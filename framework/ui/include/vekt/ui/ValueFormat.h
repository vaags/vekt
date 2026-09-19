#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <optional>
#include <sstream>
#include <locale>

namespace vekt::ui
{
enum class ValueFormat { decibels, tone, decimal, percent, frequency };

inline juce::String formatValue(double value, ValueFormat format)
{
	const auto number = [](double v, int decimals)
	{
		if (std::abs(v) < 0.5 / std::pow(10.0, decimals))
			v = 0.0;
		return juce::String(v, decimals);
	};
	switch (format)
	{
	case ValueFormat::decibels: return number(value, 1) + " dB";
	case ValueFormat::tone: return number(value, 1) + " dB/oct";
	case ValueFormat::decimal: return number(value, 3);
	case ValueFormat::percent:
		return number(value, std::abs(value - std::round(value)) < 0.00001 ? 0 : 1) + "%";
	case ValueFormat::frequency:
		return value >= 1000.0 ? number(value / 1000.0, 2) + " kHz" : number(value, 0) + " Hz";
	}
	return {};
}

inline std::optional<double> parseValue(juce::String text, ValueFormat format)
{
	text = text.trim().replace(juce::String::charToString(0x2212), "-");
	std::istringstream stream(text.toStdString());
	stream.imbue(std::locale::classic());
	double value {};
	if (!(stream >> value) || !std::isfinite(value))
		return std::nullopt;
	std::string remainder;
	std::getline(stream, remainder);
	const auto unit = juce::String(remainder).trim().toLowerCase();
	if (unit.isNotEmpty())
	{
		if (format == ValueFormat::frequency && unit == "khz")
			value *= 1000.0;
		else if (!((format == ValueFormat::frequency && unit == "hz")
			|| (format == ValueFormat::decibels && unit == "db")
			|| (format == ValueFormat::tone && unit == "db/oct")
			|| (format == ValueFormat::percent && unit == "%")))
			return std::nullopt;
	}
	return std::isfinite(value) ? std::optional<double>(value) : std::nullopt;
}

inline void configureValueFormat(juce::Slider& slider, ValueFormat format)
{
	slider.textFromValueFunction = [format](double value) { return formatValue(value, format); };
	slider.valueFromTextFunction = [&slider, format](const juce::String& text)
	{
		slider.getProperties().set("valueEntryError", false);
		// Merely opening and committing a rounded display must not quantise the parameter.
		if (text.trim() == formatValue(slider.getValue(), format))
			return slider.getValue();
		const auto parsed = parseValue(text, format);
		slider.getProperties().set("valueEntryError", !parsed.has_value());
		return parsed.value_or(slider.getValue());
	};
	slider.updateText();
}
}