#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace vekt::ui
{
class LevelMeter final : public juce::Component
{
public:
	explicit LevelMeter(juce::String name, juce::Colour colour);

	void setLevel(float linearGain) noexcept;
	void paint(juce::Graphics&) override;

private:
	juce::String name;
	juce::Colour meterColour;
	float level = 0.0f;
};
}
