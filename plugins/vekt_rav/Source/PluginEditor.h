#pragma once

#include "PluginProcessor.h"

#include <vekt/ui/LevelMeter.h>
#include <vekt/ui/Panel.h>
#include <vekt/ui/RotaryControl.h>
#include <vekt/ui/ScalableEditor.h>
#include <vekt/ui/VektLookAndFeel.h>

#include <array>
#include <memory>

namespace vekt::rav
{
class PluginEditor final : public ui::ScalableEditor,
						   private juce::Timer
{
public:
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
	void configureRotary(juce::Component& parent, ui::RotaryControl& control, const juce::String& name,
		const char* parameterId, std::unique_ptr<SliderAttachment>& attachment);

	PluginProcessor& pluginProcessor;
	ui::VektLookAndFeel lookAndFeel;
	juce::Label title;
	juce::Label presetLabel;
	juce::Label qualityLabel;
	juce::Label meterLabel;
	ui::Panel primaryPanel { "Primary" };
	ui::Panel characterPanel { "Character" };
	ui::Panel bandMixPanel { "Band Mix" };
	ui::Panel outputPanel { "Output" };
	juce::Label stageHeader;
	juce::TextButton previousButton { "<" };
	juce::TextButton nextButton { ">" };
	juce::TextButton undoButton { "Undo" };
	juce::TextButton redoButton { "Redo" };
	juce::ToggleButton bypassButton { "Bypass" };
	juce::ToggleButton autoGainButton { "Auto gain" };
    juce::ComboBox trackingBox;
    juce::ComboBox offlineBox;
    juce::ComboBox modeBox;
	std::array<juce::ToggleButton, 4> stageButtons;
	std::array<juce::TextButton, 4> stageUpButtons;
	std::array<juce::TextButton, 4> stageDownButtons;
	std::array<std::unique_ptr<ButtonAttachment>, 4> stageButtonAttachments;
	std::array<ui::RotaryControl, 6> sliders;
	std::array<std::unique_ptr<SliderAttachment>, 6> sliderAttachments;
	std::array<ui::RotaryControl, 3> macroSliders;
	std::array<std::unique_ptr<SliderAttachment>, 3> macroAttachments;
	std::array<ui::RotaryControl, 3> bandMixSliders;
	std::array<std::unique_ptr<SliderAttachment>, 3> bandMixAttachments;
	std::array<ui::RotaryControl, 2> cutoffSliders;
	std::array<std::unique_ptr<SliderAttachment>, 2> cutoffAttachments;
	ui::LevelMeter inputMeter { "IN", juce::Colour::fromRGB(91, 162, 150) };
	ui::LevelMeter outputMeter { "OUT", juce::Colour::fromRGB(227, 156, 75) };
	std::unique_ptr<ButtonAttachment> bypassAttachment;
	std::unique_ptr<ButtonAttachment> autoGainAttachment;
    std::unique_ptr<ComboBoxAttachment> trackingAttachment;
    std::unique_ptr<ComboBoxAttachment> offlineAttachment;
    std::unique_ptr<ComboBoxAttachment> modeAttachment;
	std::array<float, 2> inputPeaks {};
	std::array<float, 2> outputPeaks {};
};
}
