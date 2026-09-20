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

#include <array>
#include <memory>

namespace vekt::mono
{
class PluginEditor final : public ui::ScalableEditor, private juce::Timer
{
public:
	explicit PluginEditor(PluginProcessor& processor);
	~PluginEditor() override;
	void paint(juce::Graphics&) override;
	void resized() override;

private:
	using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
	using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
	void timerCallback() override;
	void refreshPresetLabel();
	void addRotary(ui::Panel& panel, ui::RotaryControl& control, const char* name, const char* identifier,
		std::unique_ptr<SliderAttachment>& attachment);
	void addChoice(ui::Panel& panel, juce::ComboBox& box, const juce::StringArray& choices, const char* identifier,
		std::unique_ptr<ComboBoxAttachment>& attachment);

	PluginProcessor& pluginProcessor;
	ui::VektLookAndFeel lookAndFeel;
	juce::Label title;
	juce::Label status;
	ui::UndoRedoControls historyControls;
	ui::PresetNavigation presetNavigation;
	preset_ui::PresetBrowser presetBrowser;
	ui::Panel oscillatorPanel { "Oscillators & Mixer" };
	ui::Panel filterPanel { "Ladder Filter" };
	ui::Panel voicePanel { "Voice" };
	ui::Panel ioPanel { "I/O" };
	ui::Panel ampPanel { "Amp ADSR" };
	ui::Panel filterEnvelopePanel { "Filter ADSR" };
	ui::Panel performancePanel { "Performance / Noise" };
	std::array<ui::RotaryControl, 16> oscillatorControls;
	std::array<std::unique_ptr<SliderAttachment>, 16> oscillatorAttachments;
	std::array<ui::RotaryControl, 5> filterControls;
	std::array<std::unique_ptr<SliderAttachment>, 5> filterAttachments;
	std::array<ui::RotaryControl, 5> ampControls;
	std::array<std::unique_ptr<SliderAttachment>, 5> ampAttachments;
	std::array<ui::RotaryControl, 5> filterEnvelopeControls;
	std::array<std::unique_ptr<SliderAttachment>, 5> filterEnvelopeAttachments;
	std::array<ui::RotaryControl, 4> voiceControls;
	std::array<std::unique_ptr<SliderAttachment>, 4> voiceAttachments;
	juce::Slider outputFader;
	ui::LevelMeter outputMeter { "OUT", juce::Colour::fromRGB(227, 156, 75), ui::LevelMeter::Orientation::vertical };
	std::unique_ptr<SliderAttachment> outputAttachment;
	juce::ComboBox voiceCountBox, performanceModeBox, qualityBox, unisonBox, noiseBox, glideBox;
	std::array<juce::Label, 6> performanceLabels;
	std::unique_ptr<ComboBoxAttachment> voiceCountAttachment, performanceModeAttachment, qualityAttachment, unisonAttachment, noiseAttachment, glideAttachment;
};
}