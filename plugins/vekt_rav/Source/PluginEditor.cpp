#include "PluginEditor.h"

#include "Parameters.h"

namespace vekt::rav
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
	title.setText("VEKT  RAV", juce::dontSendNotification);
	title.setFont(juce::FontOptions(22.0f).withStyle("Bold"));
	presetLabel.setJustificationType(juce::Justification::centred);
	qualityLabel.setJustificationType(juce::Justification::centredRight);
	meterLabel.setJustificationType(juce::Justification::centred);
	for (juce::Component *component : {static_cast<juce::Component *>(&title),
									   static_cast<juce::Component *>(&presetLabel), static_cast<juce::Component *>(&qualityLabel),
									   static_cast<juce::Component *>(&meterLabel), static_cast<juce::Component *>(&previousButton),
									   static_cast<juce::Component *>(&nextButton), static_cast<juce::Component *>(&undoButton),
									   static_cast<juce::Component *>(&redoButton), static_cast<juce::Component *>(&bypassButton),
									   static_cast<juce::Component *>(&autoGainButton), static_cast<juce::Component *>(&trackingBox),
									   static_cast<juce::Component *>(&offlineBox), static_cast<juce::Component *>(&modeBox)})
	{
		getContent().addAndMakeVisible(*component);
	}
	for (std::size_t index = 0; index < stageButtons.size(); ++index)
	{
		constexpr std::array names { "Saturation", "Overdrive", "Distortion", "Fuzz", "Wavefold", "Bitcrush" };
		stageButtons[index].setButtonText(names[index]);
		stageButtons[index].setToggleState(pluginProcessor.getParameters().getParameter(parameters::stageEnabledIds[index])->getValue() > 0.5f, juce::dontSendNotification);
		getContent().addAndMakeVisible(stageButtons[index]);
		stageButtonAttachments[index] = std::make_unique<ButtonAttachment>(
			pluginProcessor.getParameters(), parameters::stageEnabledIds[index], stageButtons[index]);
		stageUpButtons[index].setButtonText("↑");
		stageUpButtons[index].setVisible(index > 0);
		stageUpButtons[index].onClick = [this, index] { pluginProcessor.reorderStage(index, -1); };
		getContent().addAndMakeVisible(stageUpButtons[index]);
		stageDownButtons[index].setButtonText("↓");
		stageDownButtons[index].setVisible(index < stageDownButtons.size() - 1);
		stageDownButtons[index].onClick = [this, index] { pluginProcessor.reorderStage(index, 1); };
		getContent().addAndMakeVisible(stageDownButtons[index]);
	}

	for (std::size_t index = 0; index < sliders.size(); ++index)
		configureRotary(sliders[index], sliderLabels[index], parameterNames[index], parameterIds[index], sliderAttachments[index]);
	sliders[2].setTooltip("Tone: positive values brighten, negative values darken");
	for (std::size_t index = 0; index < macroSliders.size(); ++index)
	{
		constexpr std::array names { "Character", "Response", "Texture" };
		constexpr std::array ids { parameters::character, parameters::response, parameters::texture };
		configureRotary(macroSliders[index], macroLabels[index], names[index], ids[index], macroAttachments[index]);
	}
	for (std::size_t index = 0; index < bandMixSliders.size(); ++index)
	{
		constexpr std::array names { "Low Mix", "Mid Mix", "High Mix" };
		constexpr std::array ids { parameters::lowBandMix, parameters::midBandMix, parameters::highBandMix };
		configureRotary(bandMixSliders[index], bandMixLabels[index], names[index], ids[index], bandMixAttachments[index]);
	}
	for (std::size_t index = 0; index < cutoffSliders.size(); ++index)
	{
		constexpr std::array names { "Low-Mid Hz", "Mid-High Hz" };
		constexpr std::array ids { parameters::lowMidCutoffHz, parameters::midHighCutoffHz };
		configureRotary(cutoffSliders[index], cutoffLabels[index], names[index], ids[index], cutoffAttachments[index]);
	}

	trackingBox.addItem("Off", 1);
	trackingBox.addItem("2x IIR", 2);
	trackingBox.addItem("4x IIR", 3);
	offlineBox.addItem("Off", 1);
	offlineBox.addItem("2x FIR", 2);
	offlineBox.addItem("4x FIR", 3);
	offlineBox.addItem("8x FIR", 4);
	offlineBox.addItem("16x FIR", 5);
	modeBox.addItemList({"Saturation", "Overdrive", "Distortion", "Fuzz", "Wavefold", "Bitcrush"}, 1);
	trackingAttachment = std::make_unique<ComboBoxAttachment>(pluginProcessor.getParameters(), parameters::trackingOversampling, trackingBox);
    offlineAttachment = std::make_unique<ComboBoxAttachment>(pluginProcessor.getParameters(), parameters::offlineOversampling, offlineBox);
    modeAttachment = std::make_unique<ComboBoxAttachment>(pluginProcessor.getParameters(), parameters::mode, modeBox);
	bypassAttachment = std::make_unique<ButtonAttachment>(pluginProcessor.getParameters(), parameters::bypass, bypassButton);
	autoGainAttachment = std::make_unique<ButtonAttachment>(pluginProcessor.getParameters(), parameters::autoGain, autoGainButton);

	previousButton.onClick = [this] { juce::ignoreUnused(pluginProcessor.loadPreviousPreset()); refreshPresetLabel(); };
	nextButton.onClick = [this] { juce::ignoreUnused(pluginProcessor.loadNextPreset()); refreshPresetLabel(); };
	undoButton.onClick = [this] { pluginProcessor.getUndoManager().undo(); refreshPresetLabel(); };
	redoButton.onClick = [this]
	{ pluginProcessor.getUndoManager().redo(); refreshPresetLabel(); };
	refreshPresetLabel();
	resized();
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
	const auto scale = static_cast<float>(getWidth()) / ui::ScalableEditor::logicalWidth;
	graphics.drawLine(20.0f * scale, 58.0f * scale,
		700.0f * scale, 58.0f * scale);
	graphics.drawLine(20.0f * scale, 370.0f * scale,
		700.0f * scale, 370.0f * scale);
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
	modeBox.setBounds(280, 62, 160, 26);
	modeBox.setVisible(false);
	for (std::size_t index = 0; index < stageButtons.size(); ++index)
	{
		const auto row = static_cast<int>(index);
		const auto y = 82 + row * 26;
		stageButtons[index].setBounds(24, y, 140, 22);
		stageUpButtons[index].setBounds(170, y, 28, 22);
		stageDownButtons[index].setBounds(202, y, 28, 22);
	}
	for (std::size_t index = 0; index < sliders.size(); ++index)
	{
		const auto column = static_cast<int>(index % 3);
		const auto row = static_cast<int>(index / 3);
		const auto x = 40 + column * 230;
		const auto y = 82 + row * 102 + 180;
		sliders[index].setBounds(x, y, 110, 68);
		sliderLabels[index].setBounds(x - 20, y + 68, 150, 20);
	}
	for (std::size_t index = 0; index < macroSliders.size(); ++index)
	{
		const auto x = 180 + static_cast<int>(index) * 130;
		macroSliders[index].setBounds(x, 286, 90, 42);
		macroLabels[index].setBounds(x - 10, 329, 110, 18);
	}
	for (std::size_t index = 0; index < bandMixSliders.size(); ++index)
	{
		const auto x = 30 + static_cast<int>(index) * 115;
		bandMixSliders[index].setBounds(x, 350, 78, 40);
		bandMixLabels[index].setBounds(x - 10, 391, 100, 18);
	}
	for (std::size_t index = 0; index < cutoffSliders.size(); ++index)
	{
		const auto x = 385 + static_cast<int>(index) * 145;
		cutoffSliders[index].setBounds(x, 350, 88, 40);
		cutoffLabels[index].setBounds(x - 15, 391, 120, 18);
	}
	autoGainButton.setBounds(24, 420, 96, 26);
	trackingBox.setBounds(130, 420, 108, 26);
	offlineBox.setBounds(248, 420, 118, 26);
	qualityLabel.setBounds(378, 420, 190, 26);
	meterLabel.setBounds(24, 452, 544, 20);
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
    qualityLabel.setText("Quality: " + juce::String(static_cast<int>(quality.multiplier())) + "x " + (quality.filter == dsp::OversamplingFilter::polyphaseFIR ? "FIR" : "IIR") + (pluginProcessor.hasPendingQualityChange() ? " (pending)" : ""), juce::dontSendNotification);
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
