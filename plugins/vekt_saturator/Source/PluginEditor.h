#pragma once

#include "PluginProcessor.h"

#include <vekt/ui/ScalableEditor.h>
#include <vekt/ui/VektLookAndFeel.h>

#include <array>
#include <memory>

namespace vekt::saturator
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
	void configureRotary(juce::Slider& slider, juce::Label& label, const juce::String& name,
		const char* parameterId, std::unique_ptr<SliderAttachment>& attachment);

	PluginProcessor& pluginProcessor;
	ui::VektLookAndFeel lookAndFeel;
	juce::Label title;
	juce::Label presetLabel;
	juce::Label qualityLabel;
	juce::Label meterLabel;
	juce::TextButton previousButton { "<" };
	juce::TextButton nextButton { ">" };
	juce::TextButton undoButton { "Undo" };
	juce::TextButton redoButton { "Redo" };
	juce::ToggleButton bypassButton { "Bypass" };
	juce::ToggleButton autoGainButton { "Auto gain" };
	juce::ComboBox factorBox;
	juce::ComboBox phaseBox;
	std::array<juce::Slider, 6> sliders;
	std::array<juce::Label, 6> sliderLabels;
	std::array<std::unique_ptr<SliderAttachment>, 6> sliderAttachments;
	std::unique_ptr<ButtonAttachment> bypassAttachment;
	std::unique_ptr<ButtonAttachment> autoGainAttachment;
	std::unique_ptr<ComboBoxAttachment> factorAttachment;
	std::unique_ptr<ComboBoxAttachment> phaseAttachment;
	std::array<float, 2> inputPeaks {};
	std::array<float, 2> outputPeaks {};
};
}