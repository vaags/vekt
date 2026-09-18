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

namespace layout
{
constexpr int margin = 28;
constexpr int topBarMargin = 24;
constexpr int topBarTop = 10;
constexpr int topBarHeight = 38;
constexpr int modeTop = 62;
constexpr int modeHeight = 28;
constexpr int headerTop = 104;
constexpr int sectionGap = 30;
constexpr int panelGap = 16;
constexpr int centerWidth = 500;
constexpr int rightWidth = 334;
constexpr int controlHeight = 112;
constexpr int labelHeight = 20;
constexpr int bandLabelHeight = 30;
constexpr int rowGap = 12;
constexpr int controlGap = 18;
constexpr int outputButtonHeight = 28;
constexpr int qualityControlHeight = 26;
constexpr int meterHeight = 14;
constexpr int meterGap = 12;
constexpr int meterRowGap = 8;
constexpr int headerHeight = 22;
constexpr int footerReserve = 100;
constexpr int footerLine = ui::ScalableEditor::logicalHeight - footerReserve;
constexpr int panelContentHeight = footerLine - headerTop;
constexpr int panelHeight = (panelContentHeight - panelGap) / 2;
constexpr int bodyHeight = panelContentHeight;
constexpr int lowerPanelTop = headerTop + panelHeight + panelGap;
}
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
	stageHeader.setText("Stage Order", juce::dontSendNotification);
	for (auto* panel : { static_cast<juce::Component*>(&primaryPanel), static_cast<juce::Component*>(&characterPanel),
		static_cast<juce::Component*>(&bandMixPanel), static_cast<juce::Component*>(&outputPanel) })
		getContent().addAndMakeVisible(*panel);
	for (auto* component : { static_cast<juce::Component*>(&title),
							static_cast<juce::Component*>(&presetLabel), static_cast<juce::Component*>(&stageHeader),
							static_cast<juce::Component*>(&previousButton), static_cast<juce::Component*>(&nextButton),
							static_cast<juce::Component*>(&undoButton), static_cast<juce::Component*>(&redoButton),
							static_cast<juce::Component*>(&modeBox) })
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
		stageUpButtons[index].onClick = [this, index]
		{
			juce::ignoreUnused(pluginProcessor.reorderStage(index, -1));
		};
		getContent().addAndMakeVisible(stageUpButtons[index]);
		stageDownButtons[index].setButtonText("↓");
		stageDownButtons[index].setVisible(index < stageDownButtons.size() - 1);
		stageDownButtons[index].onClick = [this, index]
		{
			juce::ignoreUnused(pluginProcessor.reorderStage(index, 1));
		};
		getContent().addAndMakeVisible(stageDownButtons[index]);
	}

	for (std::size_t index = 0; index < sliders.size(); ++index)
		configureRotary(primaryPanel, sliders[index], parameterNames[index], parameterIds[index], sliderAttachments[index]);
	sliders[2].getSlider().setTooltip("Tone: positive values brighten, negative values darken");
	for (std::size_t index = 0; index < macroSliders.size(); ++index)
	{
		constexpr std::array names { "Character", "Response", "Texture" };
		constexpr std::array ids { parameters::character, parameters::response, parameters::texture };
		configureRotary(characterPanel, macroSliders[index], names[index], ids[index], macroAttachments[index]);
	}
	for (std::size_t index = 0; index < bandMixSliders.size(); ++index)
	{
		constexpr std::array names { "Low Mix", "Mid Mix", "High Mix" };
		constexpr std::array ids { parameters::lowBandMix, parameters::midBandMix, parameters::highBandMix };
		configureRotary(bandMixPanel, bandMixSliders[index], names[index], ids[index], bandMixAttachments[index]);
	}
	for (std::size_t index = 0; index < cutoffSliders.size(); ++index)
	{
		constexpr std::array names { "Low-Mid Hz", "Mid-High Hz" };
		constexpr std::array ids { parameters::lowMidCutoffHz, parameters::midHighCutoffHz };
		configureRotary(bandMixPanel, cutoffSliders[index], names[index], ids[index], cutoffAttachments[index]);
	}
	for (auto* component : { static_cast<juce::Component*>(&autoGainButton), static_cast<juce::Component*>(&bypassButton),
		static_cast<juce::Component*>(&trackingBox), static_cast<juce::Component*>(&offlineBox),
		static_cast<juce::Component*>(&qualityLabel), static_cast<juce::Component*>(&inputMeter),
		static_cast<juce::Component*>(&outputMeter), static_cast<juce::Component*>(&meterLabel) })
		outputPanel.addAndMakeVisible(*component);

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
}

void PluginEditor::resized()
{
	ScalableEditor::resized();
	auto& content = getContent();

	const auto contentBounds = content.getLocalBounds();
	const auto centerArea = contentBounds.withX(layout::margin).withY(layout::headerTop)
		.withWidth(layout::centerWidth).withHeight(layout::bodyHeight);
	const auto rightArea = centerArea.withX(centerArea.getRight() + layout::sectionGap)
		.withWidth(layout::rightWidth);
	primaryPanel.setBounds(centerArea.withHeight(layout::panelHeight));
	characterPanel.setBounds(centerArea.withY(layout::lowerPanelTop).withHeight(layout::panelHeight));
	bandMixPanel.setBounds(rightArea.withHeight(layout::panelHeight));
	outputPanel.setBounds(rightArea.withY(layout::lowerPanelTop).withHeight(layout::panelHeight));

	juce::FlexBox toolbar;
	toolbar.flexDirection = juce::FlexBox::Direction::row;
	toolbar.alignItems = juce::FlexBox::AlignItems::center;
	toolbar.items.add(juce::FlexItem(title).withWidth(180.0f).withHeight(32.0f));
	toolbar.items.add(juce::FlexItem(presetLabel).withFlex(1.0f).withHeight(32.0f).withMargin({ 0.0f, 18.0f, 0.0f, 18.0f }));
	toolbar.items.add(juce::FlexItem(previousButton).withWidth(32.0f).withHeight(28.0f).withMargin({ 0.0f, 4.0f, 0.0f, 4.0f }));
	toolbar.items.add(juce::FlexItem(nextButton).withWidth(32.0f).withHeight(28.0f).withMargin({ 0.0f, 4.0f, 0.0f, 4.0f }));
	toolbar.items.add(juce::FlexItem(undoButton).withWidth(54.0f).withHeight(28.0f).withMargin({ 0.0f, 4.0f, 0.0f, 4.0f }));
	toolbar.items.add(juce::FlexItem(redoButton).withWidth(54.0f).withHeight(28.0f).withMargin({ 0.0f, 4.0f, 0.0f, 4.0f }));
	toolbar.performLayout(contentBounds.withX(layout::topBarMargin).withY(layout::topBarTop)
		.withWidth(contentBounds.getWidth() - layout::topBarMargin * 2).withHeight(layout::topBarHeight).toFloat());
	modeBox.setBounds({});
	modeBox.setVisible(false);

	stageHeader.setVisible(false);
	for (std::size_t index = 0; index < stageButtons.size(); ++index)
	{
		const auto cellWidth = (contentBounds.getWidth() - layout::margin * 2) / static_cast<int>(stageButtons.size());
		const auto x = layout::margin + static_cast<int>(index) * cellWidth;
		stageButtons[index].setBounds(x, layout::modeTop, cellWidth - 40, layout::modeHeight);
		stageUpButtons[index].setBounds(x + cellWidth - 36, layout::modeTop, 16, layout::modeHeight);
		stageDownButtons[index].setBounds(x + cellWidth - 18, layout::modeTop, 16, layout::modeHeight);
	}

	auto layoutRotaryRow = [](auto& controls, std::size_t first, std::size_t count,
		juce::Rectangle<int> area, int controlHeight, int labelHeight)
	{
		juce::FlexBox row;
		row.flexDirection = juce::FlexBox::Direction::row;
		row.justifyContent = juce::FlexBox::JustifyContent::spaceBetween;
		row.alignItems = juce::FlexBox::AlignItems::flexStart;
		for (std::size_t index = first; index < first + count; ++index)
			row.items.add(juce::FlexItem(controls[index]).withFlex(1.0f).withHeight(static_cast<float>(controlHeight)).withMargin({ 0.0f, 4.0f, 0.0f, 4.0f }));
		row.performLayout(area.toFloat());
		juce::ignoreUnused(labelHeight);
	};

	const auto primaryContent = primaryPanel.getContentBounds();
	const auto characterContent = characterPanel.getContentBounds();
	const auto bandMixContent = bandMixPanel.getContentBounds();
	const auto outputContent = outputPanel.getContentBounds();
	layoutRotaryRow(sliders, 0, 3, primaryContent.withHeight(layout::controlHeight),
		layout::controlHeight, layout::labelHeight);
	layoutRotaryRow(sliders, 3, 3, primaryContent.withTrimmedTop(layout::controlHeight + layout::rowGap).withHeight(layout::controlHeight),
		layout::controlHeight, layout::labelHeight);
	layoutRotaryRow(macroSliders, 0, macroSliders.size(), characterContent.withHeight(layout::controlHeight),
		layout::controlHeight, layout::labelHeight);

	layoutRotaryRow(bandMixSliders, 0, bandMixSliders.size(), bandMixContent.withHeight(layout::controlHeight), layout::controlHeight, layout::bandLabelHeight);
	layoutRotaryRow(cutoffSliders, 0, cutoffSliders.size(), bandMixContent.withTrimmedTop(layout::controlHeight + layout::rowGap).withHeight(layout::controlHeight), layout::controlHeight, layout::bandLabelHeight);

	juce::FlexBox outputButtons;
	outputButtons.flexDirection = juce::FlexBox::Direction::row;
	outputButtons.justifyContent = juce::FlexBox::JustifyContent::spaceBetween;
	outputButtons.items.add(juce::FlexItem(autoGainButton).withFlex(1.0f).withHeight(static_cast<float>(layout::outputButtonHeight)).withMargin({ 0.0f, 4.0f, 0.0f, 4.0f }));
	outputButtons.items.add(juce::FlexItem(bypassButton).withFlex(1.0f).withHeight(static_cast<float>(layout::outputButtonHeight)).withMargin({ 0.0f, 4.0f, 0.0f, 4.0f }));
	outputButtons.performLayout(outputContent.withHeight(layout::outputButtonHeight).toFloat());
	juce::FlexBox qualityControls;
	qualityControls.flexDirection = juce::FlexBox::Direction::row;
	qualityControls.justifyContent = juce::FlexBox::JustifyContent::spaceBetween;
	qualityControls.items.add(juce::FlexItem(trackingBox).withFlex(1.0f).withHeight(static_cast<float>(layout::qualityControlHeight)).withMargin({ 0.0f, 4.0f, 0.0f, 4.0f }));
	qualityControls.items.add(juce::FlexItem(offlineBox).withFlex(1.0f).withHeight(static_cast<float>(layout::qualityControlHeight)).withMargin({ 0.0f, 4.0f, 0.0f, 4.0f }));
	qualityControls.performLayout(outputContent.withTrimmedTop(layout::outputButtonHeight + layout::controlGap)
		.withHeight(layout::qualityControlHeight).toFloat());
	qualityLabel.setBounds(outputContent.withTrimmedTop(layout::outputButtonHeight).withHeight(layout::headerHeight));
	inputMeter.setBounds(outputContent.withTrimmedTop(layout::outputButtonHeight + layout::controlGap + layout::qualityControlHeight + layout::controlGap)
		.withHeight(layout::meterHeight));
	outputMeter.setBounds(inputMeter.getBounds().translated(0, layout::meterHeight + layout::meterRowGap));
	meterLabel.setBounds(outputMeter.getBounds().translated(0, layout::meterHeight + layout::meterGap).withHeight(layout::headerHeight));
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
	inputMeter.setLevel(std::max(inputPeaks[0], inputPeaks[1]));
	outputMeter.setLevel(std::max(outputPeaks[0], outputPeaks[1]));
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

void PluginEditor::configureRotary(juce::Component& parent, ui::RotaryControl& control, const juce::String& name,
	const char* parameterId, std::unique_ptr<SliderAttachment>& attachment)
{
	control.setLabel(name);
	parent.addAndMakeVisible(control);
	attachment = std::make_unique<SliderAttachment>(pluginProcessor.getParameters(), parameterId, control.getSlider());
}
}
