#pragma once

#include <vekt/ui/StereoMeterBallistics.h>

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
	void setStereoLevels(StereoMeterBallistics::Levels linearGains) noexcept;
	void paint(juce::Graphics&) override;

private:
	juce::String name;
	juce::Colour meterColour;
	Orientation orientation;
	StereoMeterBallistics ballistics;
};
}
