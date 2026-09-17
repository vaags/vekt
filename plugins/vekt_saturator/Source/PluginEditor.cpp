#include "PluginEditor.h"

#include "Parameters.h"

namespace vekt::saturator
{
namespace
{
constexpr std::array parameterIds {
	parameters::inputGain, parameters::drive, parameters::tone,
	parameters::bias, parameters::mix, parameters::outputGain
};

constexpr std::array parameterNames { "Input", "Drive", "Tone", "Bias", "Mix", "Output" };
}

PluginEditor::PluginEditor(PluginProcessor& plugin)
	: ScalableEditor(plugin), pluginProcessor(plugin)
{
	setLookAndFeel(&lookAndFeel);
	title.setText("VEKT  SATURATOR", juce::dontSendNotification);
	title.setFont(juce::FontOptions(22.0f).withStyle("Bold"));
	presetLabel.setJustificationType(juce::Justification::centred);
	qualityLabel.setJustificationType(juce::Justification::centredRight);
	meterLabel.setJustificationType(juce::Justification::centred);
	for (juce::Component* component : { static_cast<juce::Component*>(&title),
		static_cast<juce::Component*>(&presetLabel), static_cast<juce::Component*>(&qualityLabel),
		static_cast<juce::Component*>(&meterLabel), static_cast<juce::Component*>(&previousButton),
		static_cast<juce::Component*>(&nextButton), static_cast<juce::Component*>(&undoButton),
		static_cast<juce::Component*>(&redoButton), static_cast<juce::Component*>(&bypassButton),
		static_cast<juce::Component*>(&autoGainButton), static_cast<juce::Component*>(&factorBox),
		static_cast<juce::Component*>(&phaseBox) })
		getContent().addAndMakeVisible(*component);

	for (std::size_t index = 0; index < sliders.size(); ++index)
		configureRotary(sliders[index], sliderLabels[index], parameterNames[index], parameterIds[index], sliderAttachments[index]);

	factorBox.addItem("Off", 1);
	factorBox.addItem("2x", 2);
	factorBox.addItem("4x", 3);
	phaseBox.addItem("Minimum Phase", 1);
	phaseBox.addItem("Linear Phase", 2);
	factorAttachment = std::make_unique<ComboBoxAttachment>(pluginProcessor.getParameters(), parameters::oversamplingFactor, factorBox);
	phaseAttachment = std::make_unique<ComboBoxAttachment>(pluginProcessor.getParameters(), parameters::oversamplingPhase, phaseBox);
	bypassAttachment = std::make_unique<ButtonAttachment>(pluginProcessor.getParameters(), parameters::bypass, bypassButton);
	autoGainAttachment = std::make_unique<ButtonAttachment>(pluginProcessor.getParameters(), parameters::autoGain, autoGainButton);

	previousButton.onClick = [this] { juce::ignoreUnused(pluginProcessor.loadPreviousPreset()); refreshPresetLabel(); };
	nextButton.onClick = [this] { juce::ignoreUnused(pluginProcessor.loadNextPreset()); refreshPresetLabel(); };
	undoButton.onClick = [this] { pluginProcessor.getUndoManager().undo(); refreshPresetLabel(); };
	redoButton.onClick = [this] { pluginProcessor.getUndoManager().redo(); refreshPresetLabel(); };
	factorBox.onChange = [this] { phaseBox.setEnabled(factorBox.getSelectedId() != 1); };
	refreshPresetLabel();
	startTimerHz(30);
}

PluginEditor::~PluginEditor()
{
	stopTimer();
	setLookAndFeel(nullptr);
}

void PluginEditor::paint(juce::Graphics& graphics)
{
	graphics.fillAll(lookAndFeel.findColour(juce::ResizableWindow::backgroundColourId));
	graphics.setColour(juce::Colour::fromRGB(54, 65, 70));
	graphics.drawLine(20.0f, 58.0f, 700.0f, 58.0f);
	graphics.drawLine(20.0f, 370.0f, 700.0f, 370.0f);
}

void PluginEditor::resized()
{
	ScalableEditor::resized();
	auto& content = getContent();
	title.setBounds(24, 16, 250, 32);
	presetLabel.setBounds(280, 16, 160, 32);
	previousButton.setBounds(444, 18, 30, 28);
	nextButton.setBounds(478, 18, 30, 28);
	undoButton.setBounds(514, 18, 54, 28);
	redoButton.setBounds(572, 18, 54, 28);
	bypassButton.setBounds(632, 18, 70, 28);
	for (std::size_t index = 0; index < sliders.size(); ++index)
	{
		const auto column = static_cast<int>(index % 3);
		const auto row = static_cast<int>(index / 3);
		const auto x = 70 + column * 220;
		const auto y = 90 + row * 130;
		sliders[index].setBounds(x, y, 100, 96);
		sliderLabels[index].setBounds(x - 25, y + 94, 150, 24);
	}
	autoGainButton.setBounds(30, 392, 100, 28);
	factorBox.setBounds(150, 392, 100, 28);
	phaseBox.setBounds(260, 392, 140, 28);
	qualityLabel.setBounds(414, 392, 280, 28);
	meterLabel.setBounds(30, 432, 660, 24);
	juce::ignoreUnused(content);
}

void PluginEditor::timerCallback()
{
	const auto newInputPeaks = pluginProcessor.consumeInputPeaks();
	const auto newOutputPeaks = pluginProcessor.consumeOutputPeaks();
	for (std::size_t channel = 0; channel < inputPeaks.size(); ++channel)
	{
		inputPeaks[channel] = std::max(inputPeaks[channel] * 0.88f, newInputPeaks[channel]);
		outputPeaks[channel] = std::max(outputPeaks[channel] * 0.88f, newOutputPeaks[channel]);
	}
	const auto quality = pluginProcessor.getActiveQuality();
	qualityLabel.setText("Quality: " + juce::String(static_cast<int>(quality.multiplier())) + "x "
		+ (quality.phase == dsp::OversamplingPhase::linear ? "Linear" : "Minimum")
		+ (pluginProcessor.hasPendingQualityChange() ? " (pending)" : ""), juce::dontSendNotification);
	meterLabel.setText("In " + juce::String(juce::Decibels::gainToDecibels(std::max(inputPeaks[0], inputPeaks[1]), -100.0f), 1)
		+ " dB    Out " + juce::String(juce::Decibels::gainToDecibels(std::max(outputPeaks[0], outputPeaks[1]), -100.0f), 1) + " dB", juce::dontSendNotification);
	undoButton.setEnabled(pluginProcessor.getUndoManager().canUndo());
	redoButton.setEnabled(pluginProcessor.getUndoManager().canRedo());
	repaint();
}

void PluginEditor::refreshPresetLabel()
{
	const auto index = pluginProcessor.getCurrentPresetIndex();
	const auto& entries = pluginProcessor.getPresetEntries();
	presetLabel.setText(index && *index < entries.size() ? entries[*index].name
		+ (pluginProcessor.isCurrentPresetModified() ? " *" : "") : "Untitled", juce::dontSendNotification);
}

void PluginEditor::configureRotary(juce::Slider& slider, juce::Label& label, const juce::String& name,
	const char* parameterId, std::unique_ptr<SliderAttachment>& attachment)
{
	slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
	slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 100, 22);
	slider.setName(name);
	slider.setTooltip(name);
	label.setText(name, juce::dontSendNotification);
	label.setJustificationType(juce::Justification::centred);
	getContent().addAndMakeVisible(slider);
	getContent().addAndMakeVisible(label);
	attachment = std::make_unique<SliderAttachment>(pluginProcessor.getParameters(), parameterId, slider);
}
}
