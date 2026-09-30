#pragma once

#include <vekt/mono/PluginProcessor.h>
#include <vekt/ui/LevelMeter.h>
#include <vekt/ui/Panel.h>
#include <vekt/ui/PresetNavigation.h>
#include <vekt/ui/RotaryControl.h>
#include <vekt/ui/ScalableEditor.h>
#include <vekt/ui/UndoRedoControls.h>
#include <vekt/ui/VektLookAndFeel.h>
#include <vekt/preset_ui/PresetBrowser.h>

#include <algorithm>
#include <array>
#include <memory>

namespace vekt::mono
{
// A selector tab in a panel header (the LFO and filter-type tabs): the selected tab is lit and outlined.
inline void paintHeaderTab(juce::Graphics& graphics, juce::Button& button, bool hovered, bool pressed,
	juce::Rectangle<int> textArea)
{
	const auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
	const auto fill = button.getToggleState() ? juce::Colour::fromRGB(58, 66, 68) : juce::Colour::fromRGB(31, 36, 38);
	graphics.setColour(pressed ? fill.brighter(0.12f) : hovered ? fill.brighter(0.06f) : fill);
	graphics.fillRoundedRectangle(bounds, 4.0f);
	graphics.setColour(button.getToggleState() ? juce::Colour::fromRGB(227, 156, 75) : juce::Colour::fromRGB(75, 84, 87));
	graphics.drawRoundedRectangle(bounds, 4.0f, button.hasKeyboardFocus(true) ? 2.0f : 1.0f);
	graphics.setColour(juce::Colour::fromRGB(224, 226, 220));
	graphics.setFont(juce::FontOptions(14.0f).withStyle("Bold"));
	graphics.drawText(button.getButtonText(), textArea, juce::Justification::centred);
}

// Selects which LFO the LFO panel shows; its dot glows with that LFO's live output.
class LfoTabButton final : public juce::Button
{
public:
	LfoTabButton() : Button({}) { setClickingTogglesState(true); }
	void setLevel(float newLevel)
	{
		if (juce::approximatelyEqual(level, newLevel)) return;
		level = newLevel;
		repaint();
	}
	void paintButton(juce::Graphics& graphics, bool hovered, bool pressed) override
	{
		paintHeaderTab(graphics, *this, hovered, pressed, getLocalBounds().withTrimmedRight(16));
		const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
		const auto dot = juce::Rectangle<float>(bounds.getRight() - 16.0f, bounds.getCentreY() - 4.0f, 8.0f, 8.0f);
		graphics.setColour(juce::Colour::fromRGB(227, 156, 75).withAlpha(0.2f + 0.8f * std::min(1.0f, std::abs(level))));
		graphics.fillEllipse(dot);
	}

private:
	float level {};
};

// One filter type (Ladder, SVF or K35) in the filter panel's header.
class FilterTypeTab final : public juce::Button
{
public:
	FilterTypeTab() : Button({}) {}
	void paintButton(juce::Graphics& graphics, bool hovered, bool pressed) override
	{
		paintHeaderTab(graphics, *this, hovered, pressed, getLocalBounds());
	}
};

// Horizontal bar for the live mod wheel / aftertouch amount (0..1).
class ControlMeter final : public juce::Component, public juce::SettableTooltipClient
{
public:
	void setLevel(float newLevel)
	{
		if (juce::approximatelyEqual(level, newLevel)) return;
		level = newLevel;
		repaint();
	}
	[[nodiscard]] float getLevel() const noexcept { return level; }
	void paint(juce::Graphics& graphics) override
	{
		const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
		graphics.setColour(juce::Colour::fromRGB(19, 24, 27));
		graphics.fillRoundedRectangle(bounds, 3.0f);
		graphics.setColour(juce::Colour::fromRGB(227, 156, 75));
		graphics.fillRoundedRectangle(bounds.withWidth(bounds.getWidth() * std::clamp(level, 0.0f, 1.0f)), 3.0f);
		graphics.setColour(juce::Colour::fromRGB(70, 82, 86));
		graphics.drawRoundedRectangle(bounds, 3.0f, 1.0f);
	}

private:
	float level {};
};

class PluginEditor final : public ui::ScalableEditor, private juce::Timer
{
public:
	// Mono is wider than the other products: the extra column holds the LFO (and later vibrato) panels.
	static constexpr int editorWidth = 1484;

	explicit PluginEditor(PluginProcessor& processor);
	~PluginEditor() override;
	void paint(juce::Graphics&) override;
	void resized() override;

private:
	using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
	using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
	using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
	void timerCallback() override;
	void refreshPresetLabel();
	void addRotary(ui::Panel& panel, ui::RotaryControl& control, const char* name, const char* identifier,
		std::unique_ptr<SliderAttachment>& attachment);
	void addChoice(ui::Panel& panel, juce::ComboBox& box, const juce::StringArray& choices, const char* identifier,
		std::unique_ptr<ComboBoxAttachment>& attachment);
	void selectLfo(std::size_t index);
	// Shows the filter type (0 Ladder, 1 SVF): lights its tab and disables the Ladder-only toggles for the SVF.
	void refreshFilterType();
	void selectFilterType(int type);
	void refreshLfoVisibility();

	struct LfoControls
	{
		juce::ComboBox shape, polarity, mode;
		juce::ToggleButton sync { "Sync" };
		ui::RotaryControl rate, division, amount, phase, delay, fade;
		// Same order as parameters::LfoParameterIds::depths().
		std::array<juce::Slider, 19> depths;
		std::unique_ptr<ComboBoxAttachment> shapeAttachment, polarityAttachment, modeAttachment;
		std::unique_ptr<ButtonAttachment> syncAttachment;
		std::array<std::unique_ptr<SliderAttachment>, 6> knobAttachments;
		std::array<std::unique_ptr<SliderAttachment>, 19> depthAttachments;
	};

	PluginProcessor& pluginProcessor;
	ui::VektLookAndFeel lookAndFeel;
	juce::Label title;
	juce::Label status;
	ui::UndoRedoControls historyControls;
	ui::PresetNavigation presetNavigation;
	preset_ui::PresetBrowser presetBrowser;
	std::array<ui::Panel, 3> oscillatorPanels { ui::Panel { "Osc 1" }, ui::Panel { "Osc 2" }, ui::Panel { "Osc 3" } };
	ui::Panel noisePanel { "Noise" };
	ui::Panel filterPanel { "Filter" };
	ui::Panel voicePanel { "Voice" };
	ui::Panel ioPanel { "I/O" };
	ui::Panel ampPanel { "Amp ADSR" };
	ui::Panel filterEnvelopePanel { "Filter ADSR" };
	ui::Panel performancePanel { "Performance" };
	ui::Panel lfoPanel { "LFO" };
	ui::Panel vibratoPanel { "Vibrato" };
	ui::RotaryControl vibratoRateControl, vibratoDepthControl;
	std::unique_ptr<SliderAttachment> vibratoRateAttachment, vibratoDepthAttachment;
	juce::ComboBox vibratoShapeBox;
	std::unique_ptr<ComboBoxAttachment> vibratoShapeAttachment;
	juce::Label vibratoShapeLabel, vibratoMeterLabel;
	ControlMeter vibratoMeter;
	std::array<LfoTabButton, 2> lfoTabs;
	std::array<LfoControls, 2> lfoControls;
	// Column headers (Pitch, Morph, Width, Level), oscillator rows, then the seven single destinations.
	std::array<juce::Label, 14> lfoDestinationLabels;
	std::size_t selectedLfo {};
	std::array<ui::RotaryControl, 15> oscillatorControls;
	std::array<std::unique_ptr<SliderAttachment>, 15> oscillatorAttachments;
	ui::RotaryControl noiseLevelControl;
	std::unique_ptr<SliderAttachment> noiseLevelAttachment;
	juce::Label noiseTypeLabel;
	std::array<ui::RotaryControl, 6> filterControls;
	std::array<std::unique_ptr<SliderAttachment>, 6> filterAttachments;
	std::array<FilterTypeTab, 3> filterTypeTabs;
	juce::ToggleButton qCompensationButton { "Q Compensation" };
	std::unique_ptr<ButtonAttachment> qCompensationAttachment;
	// After the tabs and toggles their callbacks update, so they are destroyed before them.
	std::unique_ptr<juce::ParameterAttachment> filterTypeAttachment, filterK35Attachment;
	// The values the attachments last delivered (their callbacks can run before the parameter state's raw values move).
	int shownFilterType {};
	bool shownK35 {};
	std::array<ui::RotaryControl, 5> ampControls;
	std::array<std::unique_ptr<SliderAttachment>, 5> ampAttachments;
	std::array<ui::RotaryControl, 5> filterEnvelopeControls;
	std::array<std::unique_ptr<SliderAttachment>, 5> filterEnvelopeAttachments;
	std::array<ui::RotaryControl, 5> voiceControls;
	std::array<std::unique_ptr<SliderAttachment>, 5> voiceAttachments;
	juce::Slider outputFader;
	ui::LevelMeter outputMeter { "OUT", juce::Colour::fromRGB(227, 156, 75), ui::LevelMeter::Orientation::vertical };
	std::unique_ptr<SliderAttachment> outputAttachment;
	juce::ComboBox voiceCountBox, performanceModeBox, qualityBox, unisonBox, noiseBox, glideBox, priorityBox, multicoreBox;
	juce::ToggleButton heldKeyReturnButton { "Held return" };
	std::array<juce::Label, 6> performanceLabels;
	juce::Label activeVoicesLabel;
	std::unique_ptr<ComboBoxAttachment> voiceCountAttachment, performanceModeAttachment, qualityAttachment, unisonAttachment, noiseAttachment, glideAttachment, priorityAttachment, multicoreAttachment;
	std::unique_ptr<ButtonAttachment> heldKeyReturnAttachment;
};
}
