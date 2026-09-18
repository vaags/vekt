#include "PluginEditor.h"

#include "Parameters.h"

namespace vekt::rav
{
PluginEditor::StageBox::StageBox(juce::String name)
	: Button(std::move(name))
{
	setClickingTogglesState(true);
	setTooltip("Click to enable or disable; drag to reorder the signal path");
}

void PluginEditor::StageBox::paintButton(juce::Graphics& graphics, bool isMouseOverButton,
	bool isButtonDown)
{
	const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
	const auto fill = getToggleState() ? juce::Colour::fromRGB(82, 116, 108)
		: juce::Colour::fromRGB(31, 36, 38);
	graphics.setColour(isButtonDown ? fill.brighter(0.12f)
		: isMouseOverButton ? fill.brighter(0.06f) : fill);
	graphics.fillRoundedRectangle(bounds, 4.0f);
	graphics.setColour(getToggleState() ? juce::Colour::fromRGB(123, 191, 173)
		: juce::Colour::fromRGB(75, 84, 87));
	graphics.drawRoundedRectangle(bounds, 4.0f, 1.0f);
	graphics.setColour(juce::Colour::fromRGB(224, 226, 220));
	graphics.setFont(juce::FontOptions(16.0f).withStyle("Bold"));
	graphics.drawText(getButtonText(), getLocalBounds().reduced(24, 0).withTrimmedBottom(14), juce::Justification::centred);
	graphics.setFont(juce::FontOptions(11.0f));
	graphics.drawText(getToggleState() ? "ON" : "OFF", getLocalBounds().removeFromBottom(18), juce::Justification::centred);
	for (int row = 0; row < 3; ++row)
		for (int column = 0; column < 2; ++column)
			graphics.fillEllipse(10.0f + static_cast<float>(column) * 5.0f,
				16.0f + static_cast<float>(row) * 5.0f, 2.0f, 2.0f);
}

void PluginEditor::StageBox::mouseDown(const juce::MouseEvent& event)
{
	wasDragged = false;
	dragOffset = event.getPosition();
}

void PluginEditor::StageBox::mouseDrag(const juce::MouseEvent& event)
{
	if (event.getDistanceFromDragStart() > 4)
	{
		wasDragged = true;
		setAlpha(0.65f);
		toFront(false);
		if (onDrag != nullptr && getParentComponent() != nullptr)
			onDrag(*this, event.getEventRelativeTo(getParentComponent()).getPosition() - dragOffset);
	}
}

void PluginEditor::StageBox::mouseUp(const juce::MouseEvent& event)
{
	setAlpha(1.0f);
	if (wasDragged)
	{
		if (onDrop != nullptr && getParentComponent() != nullptr)
			onDrop(*this, event.getEventRelativeTo(getParentComponent()).getPosition());
		return;
	}
	triggerClick();
}

namespace
{
constexpr std::array parameterIds {
	parameters::inputGain, parameters::drive, parameters::tone,
	parameters::bias, parameters::mix, parameters::outputGain
};

constexpr std::array parameterNames { "Input", "Drive", "Tone", "Bias", "Mix", "Output" };

namespace layout
{
constexpr int margin = 16;
constexpr int topBarMargin = 16;
constexpr int topBarTop = 16;
constexpr int topBarHeight = 44;
constexpr int modeTop = 80;
constexpr int modeHeight = 44;
constexpr int headerTop = 140;
constexpr int sectionGap = 16;
constexpr int panelGap = 16;
constexpr int centerWidth = 448;
constexpr int bandWidth = 288;
constexpr int rightWidth = ui::ScalableEditor::logicalWidth - margin * 2 - centerWidth - bandWidth - sectionGap * 2;
constexpr int primaryControlHeight = ui::RotaryControl::heightFor(ui::RotaryControl::Size::standard);
constexpr int bandControlHeight = ui::RotaryControl::heightFor(ui::RotaryControl::Size::compact);
constexpr int shapingControlHeight = ui::RotaryControl::heightFor(ui::RotaryControl::Size::standard);
constexpr int labelHeight = 24;
constexpr int bandLabelHeight = 24;
constexpr int outputButtonHeight = 44;
constexpr float meterSilenceFloor = 0.0001f;
constexpr int footerReserve = margin;
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
	title.setFont(juce::FontOptions(24.0f).withStyle("Bold"));
	presetLabel.setFont(juce::FontOptions(16.0f));
	qualityLabel.setFont(juce::FontOptions(14.0f));
	meterLabel.setFont(juce::FontOptions(14.0f));
	presetLabel.setJustificationType(juce::Justification::centred);
	qualityLabel.setJustificationType(juce::Justification::centredRight);
	meterLabel.setJustificationType(juce::Justification::centred);
	stageHeader.setText("Signal Path", juce::dontSendNotification);
	for (auto* panel : { static_cast<juce::Component*>(&primaryPanel), static_cast<juce::Component*>(&shapingPanel),
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
		stageButtons[index].setToggleState(pluginProcessor.getParameters().getParameter(parameters::stageEnabledIds[index])->getValue() > 0.5f, juce::dontSendNotification);
		getContent().addAndMakeVisible(stageButtons[index]);
		stageButtonAttachments[index] = std::make_unique<ButtonAttachment>(
			pluginProcessor.getParameters(), parameters::stageEnabledIds[index], stageButtons[index]);
		stageButtons[index].onDrag = [this](StageBox& box, juce::Point<int> position)
		{
			const auto minimumX = layout::margin;
			const auto maximumX = getContent().getWidth() - layout::margin - box.getWidth();
			const auto x = juce::jlimit(minimumX, maximumX, position.x);
			box.setTopLeftPosition(x, layout::modeTop);
			const auto contentWidth = getContent().getWidth() - layout::margin * 2;
			const auto target = juce::jlimit(0, static_cast<int>(RavStageChain::stageCount) - 1,
				(x + box.getWidth() / 2 - layout::margin) * static_cast<int>(RavStageChain::stageCount) / contentWidth);
			const auto& order = pluginProcessor.getStageOrder();
			const auto mode = static_cast<RavMode>(std::distance(stageButtons.data(), &box));
			auto current = static_cast<int>(std::distance(order.begin(), std::find(order.begin(), order.end(), mode)));
			while (current != target)
			{
				const auto direction = target > current ? 1 : -1;
				if (!pluginProcessor.reorderStage(static_cast<std::size_t>(current), direction))
					break;
				current += direction;
			}
			layoutStageBoxes(&box);
		};
		stageButtons[index].onDrop = [this, index](StageBox&, juce::Point<int> dropPosition)
		{
			constexpr auto stageCount = static_cast<int>(RavStageChain::stageCount);
			const auto contentWidth = getContent().getWidth() - layout::margin * 2;
			const auto target = juce::jlimit(0, stageCount - 1,
				(dropPosition.x - layout::margin) * stageCount / contentWidth);
			const auto& order = pluginProcessor.getStageOrder();
			auto current = static_cast<int>(std::distance(order.begin(), std::find(order.begin(), order.end(), static_cast<RavMode>(index))));
			while (current != target)
			{
				const auto direction = target > current ? 1 : -1;
				if (!pluginProcessor.reorderStage(static_cast<std::size_t>(current), direction))
					break;
				current += direction;
			}
			layoutStageBoxes();
		};
	}

	for (const auto index : { std::size_t { 1 }, std::size_t { 2 }, std::size_t { 3 }, std::size_t { 4 } })
		configureRotary(primaryPanel, sliders[index], parameterNames[index], parameterIds[index], sliderAttachments[index]);
	sliders[2].getSlider().setTooltip("Tone: positive values brighten, negative values darken");
	for (std::size_t index = 0; index < macroSliders.size(); ++index)
	{
		constexpr std::array names { "Shape", "Dynamics", "Texture" };
		constexpr std::array ids { parameters::shape, parameters::dynamics, parameters::texture };
		configureRotary(shapingPanel, macroSliders[index], names[index], ids[index], macroAttachments[index]);
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
		static_cast<juce::Component*>(&qualityLabel), static_cast<juce::Component*>(&inputMeter),
		static_cast<juce::Component*>(&outputMeter), static_cast<juce::Component*>(&inputFader),
		static_cast<juce::Component*>(&outputFader), static_cast<juce::Component*>(&meterLabel) })
		outputPanel.addAndMakeVisible(*component);
	getContent().addAndMakeVisible(bypassButton);
	getContent().addAndMakeVisible(settingsButton);
	getContent().addChildComponent(settingsPanel);
	trackingLabel.setText("Tracking", juce::dontSendNotification);
	offlineLabel.setText("Offline", juce::dontSendNotification);
	trackingBox.setName("Tracking quality");
	offlineBox.setName("Offline quality");
	for (auto* component : { static_cast<juce::Component*>(&trackingBox), static_cast<juce::Component*>(&offlineBox),
		static_cast<juce::Component*>(&trackingLabel), static_cast<juce::Component*>(&offlineLabel),
		static_cast<juce::Component*>(&closeSettingsButton) })
		settingsPanel.addAndMakeVisible(*component);
	settingsButton.setClickingTogglesState(true);
	settingsButton.onClick = [this]
	{
		settingsPanel.setVisible(settingsButton.getToggleState());
		if (settingsPanel.isVisible())
		{
			settingsPanel.toFront(false);
			trackingBox.grabKeyboardFocus();
		}
	};
	closeSettingsButton.onClick = [this]
	{
		settingsPanel.setVisible(false);
		settingsButton.setToggleState(false, juce::dontSendNotification);
		settingsButton.grabKeyboardFocus();
	};
	for (auto* fader : { &inputFader, &outputFader })
	{
		fader->setSliderStyle(juce::Slider::LinearVertical);
		fader->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 76, 24);
		fader->setDoubleClickReturnValue(true, 0.0);
		fader->setColour(juce::Slider::trackColourId, juce::Colour::fromRGB(91, 162, 150));
		fader->setColour(juce::Slider::thumbColourId, juce::Colour::fromRGB(244, 228, 193));
	}
	inputFader.setName("Input");
	inputFader.setTooltip("Input gain");
	outputFader.setName("Output");
	outputFader.setTooltip("Output gain");

	trackingBox.addItem("Off", 1);
	trackingBox.addItem("2x IIR", 2);
	trackingBox.addItem("4x IIR", 3);
	trackingBox.addItem("2x FIR", 4);
	trackingBox.addItem("4x FIR", 5);
	trackingBox.addItem("8x FIR", 6);
	trackingBox.addItem("16x FIR", 7);
	offlineBox.addItem("Off", 1);
	offlineBox.addItem("2x FIR", 2);
	offlineBox.addItem("4x FIR", 3);
	offlineBox.addItem("8x FIR", 4);
	offlineBox.addItem("16x FIR", 5);
	offlineBox.addItem("2x IIR", 6);
	offlineBox.addItem("4x IIR", 7);
	modeBox.addItemList({"Saturation", "Overdrive", "Distortion", "Fuzz"}, 1);
	trackingAttachment = std::make_unique<ComboBoxAttachment>(pluginProcessor.getParameters(), parameters::trackingOversampling, trackingBox);
	offlineAttachment = std::make_unique<ComboBoxAttachment>(pluginProcessor.getParameters(), parameters::offlineOversampling, offlineBox);
	modeAttachment = std::make_unique<ComboBoxAttachment>(pluginProcessor.getParameters(), parameters::mode, modeBox);
	inputFaderAttachment = std::make_unique<SliderAttachment>(pluginProcessor.getParameters(), parameters::inputGain, inputFader);
	outputFaderAttachment = std::make_unique<SliderAttachment>(pluginProcessor.getParameters(), parameters::outputGain, outputFader);
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

void PluginEditor::layoutStageBoxes(StageBox* draggedBox)
{
	const auto cellWidth = (getContent().getWidth() - layout::margin * 2)
		/ static_cast<int>(stageButtons.size());
	for (std::size_t position = 0; position < stageButtons.size(); ++position)
	{
		const auto modeIndex = static_cast<std::size_t>(pluginProcessor.getStageOrder()[position]);
		if (&stageButtons[modeIndex] != draggedBox)
			stageButtons[modeIndex].setBounds(layout::margin + static_cast<int>(position) * cellWidth,
				layout::modeTop, cellWidth - 8, layout::modeHeight + 4);
	}
}

void PluginEditor::resized()
{
	ScalableEditor::resized();
	auto& content = getContent();

	const auto contentBounds = content.getLocalBounds();
	const auto centerArea = contentBounds.withX(layout::margin).withY(layout::headerTop)
		.withWidth(layout::centerWidth).withHeight(layout::bodyHeight);
	const auto bandArea = centerArea.withX(centerArea.getRight() + layout::sectionGap)
		.withWidth(layout::bandWidth);
	const auto rightArea = bandArea.withX(bandArea.getRight() + layout::sectionGap)
		.withWidth(layout::rightWidth);
	primaryPanel.setBounds(centerArea.withHeight(layout::panelHeight));
	shapingPanel.setBounds(centerArea.withY(layout::lowerPanelTop).withHeight(layout::panelHeight));
	bandMixPanel.setBounds(bandArea);
	outputPanel.setBounds(rightArea);

	juce::FlexBox toolbar;
	toolbar.flexDirection = juce::FlexBox::Direction::row;
	toolbar.alignItems = juce::FlexBox::AlignItems::center;
	toolbar.items.add(juce::FlexItem(title).withWidth(156.0f).withHeight(44.0f));
	toolbar.items.add(juce::FlexItem(previousButton).withWidth(36.0f).withHeight(44.0f));
	toolbar.items.add(juce::FlexItem(presetLabel).withFlex(1.0f).withHeight(44.0f).withMargin({ 0.0f, 16.0f, 0.0f, 16.0f }));
	toolbar.items.add(juce::FlexItem(nextButton).withWidth(36.0f).withHeight(44.0f).withMargin({ 0.0f, 12.0f, 0.0f, 4.0f }));
	toolbar.items.add(juce::FlexItem(undoButton).withWidth(72.0f).withHeight(44.0f).withMargin({ 0.0f, 4.0f, 0.0f, 4.0f }));
	toolbar.items.add(juce::FlexItem(redoButton).withWidth(72.0f).withHeight(44.0f).withMargin({ 0.0f, 4.0f, 0.0f, 4.0f }));
	toolbar.items.add(juce::FlexItem(settingsButton).withWidth(88.0f).withHeight(44.0f).withMargin({ 0.0f, 8.0f, 0.0f, 8.0f }));
	toolbar.items.add(juce::FlexItem(bypassButton).withWidth(84.0f).withHeight(44.0f));
	toolbar.performLayout(contentBounds.withX(layout::topBarMargin).withY(layout::topBarTop)
		.withWidth(contentBounds.getWidth() - layout::topBarMargin * 2).withHeight(layout::topBarHeight).toFloat());
	modeBox.setBounds({});
	modeBox.setVisible(false);

	stageHeader.setVisible(true);
	stageHeader.setBounds(layout::margin, layout::modeTop - 20, 160, 20);
	layoutStageBoxes();

	auto layoutRotaryRow = [](auto& controls, std::size_t first, std::size_t count,
		juce::Rectangle<int> area, int controlHeight, int labelHeight)
	{
		juce::FlexBox row;
		row.flexDirection = juce::FlexBox::Direction::row;
		row.justifyContent = juce::FlexBox::JustifyContent::spaceBetween;
		row.alignItems = juce::FlexBox::AlignItems::flexStart;
		for (std::size_t index = first; index < first + count; ++index)
			row.items.add(juce::FlexItem(controls[index]).withWidth(static_cast<float>(area.getWidth()) / static_cast<float>(count) - 8.0f).withHeight(static_cast<float>(controlHeight)).withMargin({ 0.0f, 4.0f, 0.0f, 4.0f }));
		row.performLayout(area.toFloat());
		juce::ignoreUnused(labelHeight);
	};

	const auto primaryContent = primaryPanel.getContentBounds();
	const auto shapingContent = shapingPanel.getContentBounds();
	const auto bandMixContent = bandMixPanel.getContentBounds();
	const auto outputContent = outputPanel.getContentBounds();
	layoutRotaryRow(sliders, 1, 4, primaryContent.withHeight(layout::primaryControlHeight),
		layout::primaryControlHeight, layout::labelHeight);
	const auto shapingHeight = std::min(shapingContent.getHeight(), layout::shapingControlHeight);
	layoutRotaryRow(macroSliders, 0, macroSliders.size(),
		shapingContent.withWidth(primaryContent.getWidth() * 3 / 4).withHeight(shapingHeight), shapingHeight, layout::labelHeight);

	layoutRotaryRow(bandMixSliders, 0, bandMixSliders.size(), bandMixContent.withHeight(layout::bandControlHeight), layout::bandControlHeight, layout::bandLabelHeight);
	layoutRotaryRow(cutoffSliders, 0, cutoffSliders.size(), bandMixContent.withTrimmedTop(layout::panelHeight + layout::panelGap).withHeight(layout::bandControlHeight), layout::bandControlHeight, layout::bandLabelHeight);

	autoGainButton.setBounds(outputContent.withHeight(layout::outputButtonHeight));
	qualityLabel.setJustificationType(juce::Justification::centred);
	qualityLabel.setFont(juce::FontOptions(12.0f));
	qualityLabel.setBounds(outputContent.withY(outputContent.getBottom() - 36).withHeight(36));
	const auto meterArea = outputContent.withTrimmedTop(56).withTrimmedBottom(48);
	const auto stripWidth = meterArea.getWidth() / 2;
	const auto inputStrip = meterArea.withWidth(stripWidth);
	const auto outputStrip = inputStrip.translated(stripWidth, 0);
	inputFader.setBounds(inputStrip.withWidth(64));
	outputFader.setBounds(outputStrip.withWidth(64));
	inputMeter.setBounds(inputStrip.withTrimmedLeft(66).withWidth(36));
	outputMeter.setBounds(outputStrip.withTrimmedLeft(66).withWidth(36));

	settingsPanel.setBounds(contentBounds.getWidth() - layout::margin - 360, 64, 360, 224);
	const auto settingsContent = settingsPanel.getContentBounds();
	trackingLabel.setBounds(settingsContent.withHeight(24));
	trackingBox.setBounds(settingsContent.withTrimmedTop(24).withHeight(36));
	offlineLabel.setBounds(settingsContent.withTrimmedTop(68).withHeight(24));
	offlineBox.setBounds(settingsContent.withTrimmedTop(92).withHeight(36));
	closeSettingsButton.setBounds(276, 4, 72, 28);
	meterLabel.setVisible(false);
	juce::ignoreUnused(content);
}

void PluginEditor::timerCallback()
{
	const auto newInputPeaks = pluginProcessor.consumeInputPeaks();
	const auto newOutputPeaks = pluginProcessor.consumeOutputPeaks();
	const auto newInputPeak = std::max(newInputPeaks[0], newInputPeaks[1]);
	const auto newOutputPeak = std::max(newOutputPeaks[0], newOutputPeaks[1]);
	const auto clearIfSilent = [](std::array<float, 2>& peaks, float newPeak)
	{
		if (newPeak < layout::meterSilenceFloor)
			peaks.fill(0.0f);
	};
	for (std::size_t channel = 0; channel < inputPeaks.size(); ++channel)
	{
		inputPeaks[channel] = std::max(inputPeaks[channel] * 0.88f, newInputPeaks[channel]);
		outputPeaks[channel] = std::max(outputPeaks[channel] * 0.88f, newOutputPeaks[channel]);
	}
	clearIfSilent(inputPeaks, newInputPeak);
	clearIfSilent(outputPeaks, newOutputPeak);
	const auto inputPeak = std::max(inputPeaks[0], inputPeaks[1]);
	const auto outputPeak = std::max(outputPeaks[0], outputPeaks[1]);
	const auto displayedInputPeak = inputPeak < layout::meterSilenceFloor ? 0.0f : inputPeak;
	const auto displayedOutputPeak = outputPeak < layout::meterSilenceFloor ? 0.0f : outputPeak;
	inputMeter.setLevel(displayedInputPeak);
	outputMeter.setLevel(displayedOutputPeak);
	const auto quality = pluginProcessor.getActiveQuality();
	qualityLabel.setText("Quality: " + juce::String(static_cast<int>(quality.multiplier())) + "x " + (quality.filter == dsp::OversamplingFilter::polyphaseFIR ? "FIR" : "IIR") + (pluginProcessor.hasPendingQualityChange() ? " (pending)" : ""), juce::dontSendNotification);
	meterLabel.setText("In " + juce::String(juce::Decibels::gainToDecibels(displayedInputPeak, -100.0f), 1)
		+ " dB    Out " + juce::String(juce::Decibels::gainToDecibels(displayedOutputPeak, -100.0f), 1) + " dB", juce::dontSendNotification);
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
