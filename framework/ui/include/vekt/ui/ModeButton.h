#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace vekt::ui
{
class ModeButton final : public juce::Button
{
public:
	explicit ModeButton(juce::String name = {}, bool reorderable = false)
		: Button(std::move(name)), canReorder(reorderable)
	{
		setClickingTogglesState(true);
		if (canReorder) setTooltip("Click to enable or disable; drag to reorder the signal path");
	}

	std::function<void(ModeButton&, juce::Point<int>)> onDrag;
	std::function<void(ModeButton&, juce::Point<int>)> onDrop;

	void paintButton(juce::Graphics& graphics, bool hovered, bool pressed) override
	{
		const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
		const auto fill = getToggleState() ? juce::Colour::fromRGB(82, 116, 108)
			: juce::Colour::fromRGB(31, 36, 38);
		graphics.setColour((pressed ? fill.brighter(0.12f) : hovered ? fill.brighter(0.06f) : fill)
			.withMultipliedAlpha(isEnabled() ? 1.0f : 0.5f));
		graphics.fillRoundedRectangle(bounds, 4.0f);
		graphics.setColour(getToggleState() ? juce::Colour::fromRGB(123, 191, 173)
			: juce::Colour::fromRGB(75, 84, 87));
		graphics.drawRoundedRectangle(bounds, 4.0f, hasKeyboardFocus(true) ? 2.0f : 1.0f);
		graphics.setColour(juce::Colour::fromRGB(224, 226, 220));
		graphics.setFont(juce::FontOptions(16.0f).withStyle("Bold"));
		graphics.drawText(getButtonText(), getLocalBounds().reduced(canReorder ? 24 : 4, 0).withTrimmedBottom(14), juce::Justification::centred);
		graphics.setFont(juce::FontOptions(11.0f));
		graphics.drawText(canReorder ? (getToggleState() ? "ON" : "OFF") : (getToggleState() ? "SELECTED" : ""),
			getLocalBounds().removeFromBottom(18), juce::Justification::centred);
		if (canReorder)
			for (int row = 0; row < 3; ++row)
				for (int column = 0; column < 2; ++column)
					graphics.fillEllipse(10.0f + static_cast<float>(column) * 5.0f,
						16.0f + static_cast<float>(row) * 5.0f, 2.0f, 2.0f);
	}

	void mouseDown(const juce::MouseEvent& event) override
	{
		if (!canReorder) { Button::mouseDown(event); return; }
		wasDragged = false;
		dragOffset = event.getPosition();
	}

	void mouseDrag(const juce::MouseEvent& event) override
	{
		if (!canReorder) { Button::mouseDrag(event); return; }
		if (isEnabled() && event.getDistanceFromDragStart() > 4)
		{
			wasDragged = true;
			setAlpha(0.65f);
			toFront(false);
			if (onDrag && getParentComponent())
				onDrag(*this, event.getEventRelativeTo(getParentComponent()).getPosition() - dragOffset);
		}
	}

	void mouseUp(const juce::MouseEvent& event) override
	{
		if (!canReorder) { Button::mouseUp(event); return; }
		setAlpha(1.0f);
		if (!isEnabled()) return;
		if (wasDragged)
		{
			if (onDrop && getParentComponent())
				onDrop(*this, event.getEventRelativeTo(getParentComponent()).getPosition());
			return;
		}
		triggerClick();
	}

private:
	bool canReorder {};
	bool wasDragged {};
	juce::Point<int> dragOffset;
};
}