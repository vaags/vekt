#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace vekt::ui
{
class LevelMeter final : public juce::Component
{
public:
	enum class Orientation
	{
		horizontal,
		vertical
	};

	explicit LevelMeter(juce::String name, juce::Colour colour,
		Orientation orientation = Orientation::horizontal);

	void setLevel(float linearGain) noexcept;
	void paint(juce::Graphics&) override;

private:
	juce::String name;
	juce::Colour meterColour;
	Orientation orientation;
	float level = 0.0f;
};
}
