#pragma once

#include "PluginProcessor.h"

#include <vekt/ui/LevelMeter.h>
#include <vekt/ui/Panel.h>
#include <vekt/ui/RotaryControl.h>
#include <vekt/ui/ScalableEditor.h>
#include <vekt/ui/VektLookAndFeel.h>
#include <vekt/preset_ui/PresetBrowser.h>

#include <array>
#include <memory>

namespace vekt::glimmer
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
	using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
	using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
	void timerCallback() override;
	void configureRotary(ui::Panel& panel, ui::RotaryControl& control, const char* label,
		const char* parameter, std::unique_ptr<SliderAttachment>& attachment);

	PluginProcessor& pluginProcessor;
	ui::VektLookAndFeel lookAndFeel;
	juce::Label title;
	juce::TextButton presetButton { "Presets" };
	preset_ui::PresetBrowser presetBrowser;
	juce::Label autoTargetLabel;
	ui::Panel rotationPanel { "Rotation" };
	ui::Panel microphonePanel { "Cabinet & Mics" };
	ui::Panel tonePanel { "Tone & Preamp" };
	ui::Panel ioPanel { "I/O" };
	std::array<ui::RotaryControl, 5> rotationControls;
	std::array<std::unique_ptr<SliderAttachment>, 5> rotationAttachments;
	std::array<ui::RotaryControl, 3> microphoneControls;
	std::array<std::unique_ptr<SliderAttachment>, 3> microphoneAttachments;
	std::array<ui::RotaryControl, 3> toneControls;
	std::array<std::unique_ptr<SliderAttachment>, 3> toneAttachments;
	juce::ComboBox speedModeBox;
	juce::ComboBox trackingQualityBox;
	juce::ComboBox offlineQualityBox;
	juce::Label qualityLabel;
	juce::Slider inputFader;
	juce::Slider outputFader;
	juce::ToggleButton bypassButton { "Bypass" };
	juce::ToggleButton autoGainButton { "Auto gain" };
	ui::LevelMeter inputMeter { "IN", juce::Colour::fromRGB(91, 162, 150), ui::LevelMeter::Orientation::vertical };
	ui::LevelMeter outputMeter { "OUT", juce::Colour::fromRGB(227, 156, 75), ui::LevelMeter::Orientation::vertical };
	std::unique_ptr<ComboBoxAttachment> speedModeAttachment;
	std::unique_ptr<ComboBoxAttachment> trackingQualityAttachment;
	std::unique_ptr<ComboBoxAttachment> offlineQualityAttachment;
	std::unique_ptr<SliderAttachment> inputAttachment;
	std::unique_ptr<SliderAttachment> outputAttachment;
	std::unique_ptr<ButtonAttachment> bypassAttachment;
	std::unique_ptr<ButtonAttachment> autoGainAttachment;
};
}
