#include "PluginEditor.h"

#include <vekt/ui/ValueFormat.h>

namespace vekt::glimmer
{
PluginEditor::PluginEditor(PluginProcessor& newProcessor)
	: ScalableEditor(newProcessor), pluginProcessor(newProcessor),
	  presetBrowser(newProcessor.getPresetSession())
{
	setLookAndFeel(&lookAndFeel);
	title.setText("VEKT  GLIMMER", juce::dontSendNotification);
	title.setFont(juce::FontOptions(24.0f).withStyle("Bold"));
	presetButton.setTooltip("Browse, load and save presets");
	presetButton.onClick = [this]
	{
		presetBrowser.refresh();
		presetBrowser.setVisible(true);
		presetBrowser.toFront(true);
	};
	presetBrowser.onClose = [this] { presetBrowser.setVisible(false); presetButton.grabKeyboardFocus(); };
	autoTargetLabel.setJustificationType(juce::Justification::centred);
	for (auto* panel : { &rotationPanel, &microphonePanel, &tonePanel, &ioPanel })
		getContent().addAndMakeVisible(*panel);
	getContent().addAndMakeVisible(title);
	getContent().addAndMakeVisible(presetButton);
	getContent().addChildComponent(presetBrowser);
	getContent().addAndMakeVisible(autoTargetLabel);

	constexpr std::array rotationNames { "Slow", "Fast", "Accel", "Decel", "Sensitivity" };
	constexpr std::array rotationIds { parameters::slowSpeed, parameters::fastSpeed,
		parameters::accelerationTime, parameters::decelerationTime, parameters::sensitivity };
	for (std::size_t index = 0; index < rotationControls.size(); ++index)
		configureRotary(rotationPanel, rotationControls[index], rotationNames[index], rotationIds[index], rotationAttachments[index]);
	constexpr std::array microphoneNames { "Balance", "Angle", "Distance" };
	constexpr std::array microphoneIds { parameters::hornDrumBalance, parameters::micAngle, parameters::micDistance };
	for (std::size_t index = 0; index < microphoneControls.size(); ++index)
		configureRotary(microphonePanel, microphoneControls[index], microphoneNames[index], microphoneIds[index], microphoneAttachments[index]);
	constexpr std::array toneNames { "Horn Tone", "Drum Tone", "Preamp" };
	constexpr std::array toneIds { parameters::hornTone, parameters::drumTone, parameters::preampDrive };
	for (std::size_t index = 0; index < toneControls.size(); ++index)
		configureRotary(tonePanel, toneControls[index], toneNames[index], toneIds[index], toneAttachments[index]);

	speedModeBox.addItemList({ "Slow", "Fast", "Auto" }, 1);
	rotationPanel.addAndMakeVisible(speedModeBox);
	speedModeAttachment = std::make_unique<ComboBoxAttachment>(pluginProcessor.getParameters(), parameters::speedMode, speedModeBox);
	trackingQualityBox.addItemList({ "Off", "2x IIR", "4x IIR", "2x FIR", "4x FIR", "8x FIR", "16x FIR" }, 1);
	offlineQualityBox.addItemList({ "Off", "2x FIR", "4x FIR", "8x FIR", "16x FIR", "2x IIR", "4x IIR" }, 1);
	trackingQualityBox.setName("Tracking quality");
	offlineQualityBox.setName("Offline quality");
	trackingQualityBox.setTooltip("Oversampling used during real-time playback");
	offlineQualityBox.setTooltip("Oversampling used during offline rendering");
	for (auto* component : { static_cast<juce::Component*>(&trackingQualityBox), static_cast<juce::Component*>(&offlineQualityBox),
		static_cast<juce::Component*>(&qualityLabel) })
		ioPanel.addAndMakeVisible(*component);
	trackingQualityAttachment = std::make_unique<ComboBoxAttachment>(pluginProcessor.getParameters(), parameters::trackingOversampling, trackingQualityBox);
	offlineQualityAttachment = std::make_unique<ComboBoxAttachment>(pluginProcessor.getParameters(), parameters::offlineOversampling, offlineQualityBox);
	qualityLabel.setJustificationType(juce::Justification::centred);
	for (auto* component : { static_cast<juce::Component*>(&inputFader), static_cast<juce::Component*>(&outputFader),
		static_cast<juce::Component*>(&bypassButton), static_cast<juce::Component*>(&autoGainButton),
		static_cast<juce::Component*>(&inputMeter), static_cast<juce::Component*>(&outputMeter) })
		ioPanel.addAndMakeVisible(*component);
	for (auto* fader : { &inputFader, &outputFader })
	{
		fader->setSliderStyle(juce::Slider::LinearVertical);
		fader->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 76, 24);
		fader->setDoubleClickReturnValue(true, 0.0);
		fader->setColour(juce::Slider::trackColourId, juce::Colour::fromRGB(91, 162, 150));
	}
	inputAttachment = std::make_unique<SliderAttachment>(pluginProcessor.getParameters(), parameters::inputGain, inputFader);
	outputAttachment = std::make_unique<SliderAttachment>(pluginProcessor.getParameters(), parameters::outputGain, outputFader);
	bypassAttachment = std::make_unique<ButtonAttachment>(pluginProcessor.getParameters(), parameters::bypass, bypassButton);
	autoGainAttachment = std::make_unique<ButtonAttachment>(pluginProcessor.getParameters(), parameters::autoGain, autoGainButton);
	ui::configureValueFormat(inputFader, ui::ValueFormat::decibels);
	ui::configureValueFormat(outputFader, ui::ValueFormat::decibels);
	resized();
	startTimerHz(30);
}

PluginEditor::~PluginEditor()
{
	setLookAndFeel(nullptr);
}

void PluginEditor::configureRotary(ui::Panel& panel, ui::RotaryControl& control, const char* label,
	const char* parameter, std::unique_ptr<SliderAttachment>& attachment)
{
	control.setLabel(label);
	control.setLayout(ui::RotaryControl::Size::standard, 84);
	panel.addAndMakeVisible(control);
	attachment = std::make_unique<SliderAttachment>(pluginProcessor.getParameters(), parameter, control.getSlider());
}

void PluginEditor::timerCallback()
{
	inputMeter.setStereoLevels(pluginProcessor.consumeInputPeaks());
	outputMeter.setStereoLevels(pluginProcessor.consumeOutputPeaks());
	autoTargetLabel.setText(pluginProcessor.isAutoTargetFast() ? "Auto target: Fast" : "Auto target: Slow", juce::dontSendNotification);
	rotationControls[4].setEnabled(pluginProcessor.getParameters().getRawParameterValue(parameters::speedMode)->load() >= 1.5f);
	const auto quality = pluginProcessor.getActiveQuality();
	qualityLabel.setText("Quality: " + juce::String(static_cast<int>(quality.multiplier())) + "x "
		+ (quality.filter == dsp::OversamplingFilter::polyphaseFIR ? "FIR" : "IIR")
		+ (pluginProcessor.hasPendingQualityChange() ? " (pending)" : ""), juce::dontSendNotification);
}

void PluginEditor::paint(juce::Graphics& graphics)
{
	graphics.fillAll(juce::Colour::fromRGB(20, 24, 28));
}

void PluginEditor::resized()
{
	ScalableEditor::resized();
	auto& content = getContent();
	title.setBounds(16, 16, 360, 36);
	presetButton.setBounds(370, 18, 112, 32);
	autoTargetLabel.setBounds(800, 16, 224, 36);
	presetBrowser.setBounds(content.getLocalBounds().reduced(16));
	rotationPanel.setBounds(16, 68, 664, 260);
	microphonePanel.setBounds(696, 68, 328, 260);
	tonePanel.setBounds(16, 344, 664, 290);
	ioPanel.setBounds(696, 344, 328, 290);
	for (std::size_t index = 0; index < rotationControls.size(); ++index)
		rotationControls[index].setBounds(8 + static_cast<int>(index) * 130, 56, 124, 160);
	speedModeBox.setBounds(218, 216, 228, 28);
	for (std::size_t index = 0; index < microphoneControls.size(); ++index)
		microphoneControls[index].setBounds(12 + static_cast<int>(index) * 102, 64, 98, 160);
	for (std::size_t index = 0; index < toneControls.size(); ++index)
		toneControls[index].setBounds(30 + static_cast<int>(index) * 190, 70, 170, 170);
	inputFader.setBounds(24, 58, 92, 180);
	outputFader.setBounds(130, 58, 92, 180);
	inputMeter.setBounds(238, 52, 28, 190);
	outputMeter.setBounds(278, 52, 28, 190);
	bypassButton.setBounds(22, 224, 100, 28);
	autoGainButton.setBounds(136, 224, 110, 28);
	trackingQualityBox.setBounds(16, 258, 144, 24);
	offlineQualityBox.setBounds(168, 258, 144, 24);
	qualityLabel.setBounds(16, 2, 296, 22);
	juce::ignoreUnused(content);
}
}
