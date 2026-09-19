#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace vekt::ui
{
class PresetNavigation final : public juce::Component
{
public:
	PresetNavigation()
	{
		setName("Preset navigation");
		presetButton.setName("Open preset browser");
		presetButton.setTooltip("Browse, load and save presets");
		previousButton.setTooltip("Previous preset");
		nextButton.setTooltip("Next preset");
		addAndMakeVisible(previousButton);
		addAndMakeVisible(presetButton);
		addAndMakeVisible(nextButton);
		presetButton.onClick = [this] { if (onBrowse) onBrowse(); };
		previousButton.onClick = [this] { if (onPrevious) onPrevious(); };
		nextButton.onClick = [this] { if (onNext) onNext(); };
		setPreset("Untitled", false, false);
	}

	std::function<void()> onBrowse;
	std::function<void()> onPrevious;
	std::function<void()> onNext;

	void setPreset(const juce::String& name, bool modified, bool canNavigate)
	{
		presetButton.setButtonText(name + (modified ? " *" : ""));
		previousButton.setEnabled(canNavigate);
		nextButton.setEnabled(canNavigate);
	}

	void focusPreset()
	{
		if (presetButton.isShowing()) presetButton.grabKeyboardFocus();
	}

	void resized() override
	{
		const auto arrowSize = juce::jmin(24, getHeight());
		previousButton.setBounds(0, (getHeight() - arrowSize) / 2, arrowSize, arrowSize);
		nextButton.setBounds(getWidth() - arrowSize, (getHeight() - arrowSize) / 2, arrowSize, arrowSize);
		presetButton.setBounds(arrowSize + 8, 0, juce::jmax(0, getWidth() - 2 * (arrowSize + 8)), getHeight());
	}

private:
	juce::TextButton presetButton;
	juce::ArrowButton previousButton { "Previous preset", 0.5f, juce::Colour::fromRGB(224, 226, 220) };
	juce::ArrowButton nextButton { "Next preset", 0.0f, juce::Colour::fromRGB(224, 226, 220) };
};
}