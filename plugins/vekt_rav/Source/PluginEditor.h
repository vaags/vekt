#pragma once

#include "PluginProcessor.h"

#include <vekt/ui/LevelMeter.h>
#include <vekt/ui/ModeButton.h>
#include <vekt/ui/PresetNavigation.h>
#include <vekt/ui/UndoRedoControls.h>
#include <vekt/ui/Panel.h>
#include <vekt/ui/RotaryControl.h>
#include <vekt/ui/ScalableEditor.h>
#include <vekt/ui/VektLookAndFeel.h>
#include <vekt/preset_ui/PresetBrowser.h>

#include <array>
#include <functional>
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
	using StageBox = ui::ModeButton;

	using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
	using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
	using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

	void timerCallback() override;
	void refreshPresetLabel();
	void syncStageBoxOrder();
	void layoutStageBoxes(StageBox* draggedBox = nullptr);
	void configureRotary(juce::Component& parent, ui::RotaryControl& control, const juce::String& name,
		const char* parameterId, std::unique_ptr<SliderAttachment>& attachment);

	PluginProcessor& pluginProcessor;
	ui::VektLookAndFeel lookAndFeel;
	juce::Label title;
	ui::PresetNavigation presetNavigation;
	preset_ui::PresetBrowser presetBrowser;
	juce::Label qualityLabel;
	juce::Label meterLabel;
	ui::Panel primaryPanel { "Primary" };
	ui::Panel shapingPanel { "Shaping" };
	ui::Panel bandMixPanel { "Band Mix" };
	ui::Panel crossoverPanel { "Crossovers" };
	ui::Panel outputPanel { "I/O" };
	ui::Panel settingsPanel { "Quality settings" };
	juce::Label trackingLabel;
	juce::Label offlineLabel;
	juce::Label inputLabel;
	juce::Label outputLabel;
	juce::TextButton settingsButton { "Settings" };
	juce::TextButton closeSettingsButton { "Close" };
	juce::Label stageHeader;
	ui::UndoRedoControls historyControls;
	juce::ToggleButton bypassButton { "Bypass" };
	juce::ToggleButton autoGainButton { "Auto gain" };
    juce::ComboBox trackingBox;
    juce::ComboBox offlineBox;
    juce::ComboBox modeBox;
	std::array<StageBox, RavStageChain::stageCount> stageButtons { StageBox { "Saturation", true },
		StageBox { "Overdrive", true }, StageBox { "Distortion", true }, StageBox { "Circuit Fuzz", true },
		StageBox { "Gated Fuzz", true } };
	std::array<std::unique_ptr<ButtonAttachment>, RavStageChain::stageCount> stageButtonAttachments;
	std::array<ui::RotaryControl, 6> sliders;
	std::array<std::unique_ptr<SliderAttachment>, 6> sliderAttachments;
	std::array<ui::RotaryControl, 3> macroSliders;
	std::array<std::unique_ptr<SliderAttachment>, 3> macroAttachments;
	std::array<ui::RotaryControl, 3> bandMixSliders;
	std::array<std::unique_ptr<SliderAttachment>, 3> bandMixAttachments;
	std::array<ui::RotaryControl, 2> cutoffSliders;
	std::array<std::unique_ptr<SliderAttachment>, 2> cutoffAttachments;
	ui::LevelMeter inputMeter { "IN", juce::Colour::fromRGB(91, 162, 150), ui::LevelMeter::Orientation::vertical };
	ui::LevelMeter outputMeter { "OUT", juce::Colour::fromRGB(227, 156, 75), ui::LevelMeter::Orientation::vertical };
	juce::Slider inputFader;
	juce::Slider outputFader;
	std::unique_ptr<SliderAttachment> inputFaderAttachment;
	std::unique_ptr<SliderAttachment> outputFaderAttachment;
	std::unique_ptr<ButtonAttachment> bypassAttachment;
	std::unique_ptr<ButtonAttachment> autoGainAttachment;
    std::unique_ptr<ComboBoxAttachment> trackingAttachment;
    std::unique_ptr<ComboBoxAttachment> offlineAttachment;
    std::unique_ptr<ComboBoxAttachment> modeAttachment;
	RavStageChain::Order displayedStageOrder;
};
}
