#include <vekt/ui/Panel.h>

namespace vekt::ui
{
namespace
{
constexpr int cornerRadius = 6;
constexpr int headerHeight = 24;
constexpr int contentPadding = 12;
}

Panel::Panel(juce::String title)
	: titleText(std::move(title))
{
}

void Panel::setTitle(juce::String title)
{
	titleText = std::move(title);
	repaint();
}

juce::Rectangle<int> Panel::getContentBounds() const
{
	return getLocalBounds().withTrimmedTop(headerHeight).reduced(contentPadding);
}

void Panel::paint(juce::Graphics& graphics)
{
	auto bounds = getLocalBounds().toFloat();
	graphics.setColour(juce::Colour::fromRGB(27, 33, 37));
	graphics.fillRoundedRectangle(bounds, static_cast<float>(cornerRadius));
	graphics.setColour(juce::Colour::fromRGB(54, 65, 70));
	graphics.drawRoundedRectangle(bounds.reduced(0.5f), static_cast<float>(cornerRadius), 1.0f);
	graphics.setColour(juce::Colour::fromRGB(227, 156, 75));
	graphics.fillRect(bounds.removeFromTop(2.0f));
	graphics.setColour(juce::Colour::fromRGB(232, 225, 208));
	graphics.setFont(juce::FontOptions(14.0f).withStyle("Bold"));
	graphics.drawText(titleText, getLocalBounds().withHeight(headerHeight).reduced(contentPadding, 0),
		juce::Justification::centredLeft);
}
}
