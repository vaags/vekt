#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace vekt::ui
{
class Panel final : public juce::Component
{
public:
	explicit Panel(juce::String title = {});

	void setTitle(juce::String title);
	[[nodiscard]] juce::Rectangle<int> getContentBounds() const;
	void paint(juce::Graphics&) override;

private:
	juce::String titleText;
};
}
