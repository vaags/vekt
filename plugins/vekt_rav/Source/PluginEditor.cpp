#include "PluginEditor.h"

#include "Parameters.h"
#include <vekt/ui/ValueFormat.h>

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
constexpr int stageGap = 8;
constexpr int stageWidth = (ui::ScalableEditor::logicalWidth - margin * 2
	- stageGap * static_cast<int>(RavStageChain::stageCount - 1))
	/ static_cast<int>(RavStageChain::stageCount);
int stageTarget(int x)
{
	return juce::jlimit(0, static_cast<int>(RavStageChain::stageCount - 1),
		(x - margin + stageGap / 2) / (stageWidth + stageGap));
}
constexpr int footerReserve = margin;
constexpr int footerLine = ui::ScalableEditor::logicalHeight - footerReserve;
constexpr int panelContentHeight = footerLine - headerTop;
constexpr int panelHeight = (panelContentHeight - panelGap) / 2;
constexpr int bodyHeight = panelContentHeight;
constexpr int lowerPanelTop = headerTop + panelHeight + panelGap;
}
}

PluginEditor::PluginEditor(PluginProcessor& plugin)
	: ScalableEditor(plugin), pluginProcessor(plugin), presetBrowser(plugin.getPresetSession()),
	  historyControls(plugin.getUndoManager())
{
	setLookAndFeel(&lookAndFeel);
	title.setText("VEKT  RAV", juce::dontSendNotification);
	title.setFont(juce::FontOptions(24.0f).withStyle("Bold"));
	qualityLabel.setFont(juce::FontOptions(14.0f));
	meterLabel.setFont(juce::FontOptions(14.0f));
	getContent().addChildComponent(presetBrowser);
	presetNavigation.onBrowse = [this]
	{
		presetBrowser.refresh();
		presetBrowser.setVisible(true);
		presetBrowser.toFront(true);
	};
	presetBrowser.onClose = [this] { presetBrowser.setVisible(false); presetNavigation.focusPreset(); };
	presetBrowser.onSoundChanged = [this] { syncStageBoxOrder(); refreshPresetLabel(); };
	qualityLabel.setJustificationType(juce::Justification::centredRight);
	meterLabel.setJustificationType(juce::Justification::centred);
	stageHeader.setText("Signal Path", juce::dontSendNotification);
	for (auto* panel : { static_cast<juce::Component*>(&primaryPanel), static_cast<juce::Component*>(&shapingPanel),
		static_cast<juce::Component*>(&bandMixPanel), static_cast<juce::Component*>(&crossoverPanel), static_cast<juce::Component*>(&outputPanel) })
		getContent().addAndMakeVisible(*panel);
	for (auto* component : { static_cast<juce::Component*>(&title),
							static_cast<juce::Component*>(&presetNavigation), static_cast<juce::Component*>(&stageHeader),
							static_cast<juce::Component*>(&historyControls),
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
			const auto target = layout::stageTarget(x + box.getWidth() / 2);
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
			const auto target = layout::stageTarget(dropPosition.x);
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
		constexpr std::array names { "Low-Mid", "Mid-High" };
		constexpr std::array ids { parameters::lowMidCutoffHz, parameters::midHighCutoffHz };
		configureRotary(crossoverPanel, cutoffSliders[index], juce::String::fromUTF8(names[index]), ids[index], cutoffAttachments[index]);
	}
	for (auto* component : { static_cast<juce::Component*>(&autoGainButton), static_cast<juce::Component*>(&bypassButton),
		static_cast<juce::Component*>(&qualityLabel), static_cast<juce::Component*>(&inputMeter),
		static_cast<juce::Component*>(&outputMeter), static_cast<juce::Component*>(&inputFader),
		static_cast<juce::Component*>(&outputFader), static_cast<juce::Component*>(&meterLabel) })
		outputPanel.addAndMakeVisible(*component);
	inputLabel.setText("Input", juce::dontSendNotification);
	outputLabel.setText("Output", juce::dontSendNotification);
	for (auto* label : { &inputLabel, &outputLabel })
	{
		label->setJustificationType(juce::Justification::centred);
		outputPanel.addAndMakeVisible(*label);
	}
	inputFader.getProperties().set("ioFader", true);
	outputFader.getProperties().set("ioFader", true);
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
			if (trackingBox.isShowing())
				trackingBox.grabKeyboardFocus();
		}
	};
	closeSettingsButton.onClick = [this]
	{
		settingsPanel.setVisible(false);
		settingsButton.setToggleState(false, juce::dontSendNotification);
		if (settingsButton.isShowing())
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
	modeBox.addItemList({"Saturation", "Overdrive", "Distortion", "Circuit Fuzz", "Gated Fuzz"}, 1);
	trackingAttachment = std::make_unique<ComboBoxAttachment>(pluginProcessor.getParameters(), parameters::trackingOversampling, trackingBox);
	offlineAttachment = std::make_unique<ComboBoxAttachment>(pluginProcessor.getParameters(), parameters::offlineOversampling, offlineBox);
	modeAttachment = std::make_unique<ComboBoxAttachment>(pluginProcessor.getParameters(), parameters::mode, modeBox);
	inputFaderAttachment = std::make_unique<SliderAttachment>(pluginProcessor.getParameters(), parameters::inputGain, inputFader);
	outputFaderAttachment = std::make_unique<SliderAttachment>(pluginProcessor.getParameters(), parameters::outputGain, outputFader);
	ui::configureValueFormat(inputFader, ui::ValueFormat::decibels);
	ui::configureValueFormat(outputFader, ui::ValueFormat::decibels);
	bypassAttachment = std::make_unique<ButtonAttachment>(pluginProcessor.getParameters(), parameters::bypass, bypassButton);
	autoGainAttachment = std::make_unique<ButtonAttachment>(pluginProcessor.getParameters(), parameters::autoGain, autoGainButton);

	const auto reportLoad = [this](const juce::Result& result)
	{
		if (result.failed())
		{
			presetBrowser.refresh(); presetBrowser.setVisible(true); presetBrowser.toFront(true);
			presetBrowser.showResult(result);
		}
		if (result.wasOk()) syncStageBoxOrder();
		refreshPresetLabel();
	};
	presetNavigation.onPrevious = [this, reportLoad] { reportLoad(pluginProcessor.loadPreviousPreset()); };
	presetNavigation.onNext = [this, reportLoad] { reportLoad(pluginProcessor.loadNextPreset()); };
	historyControls.beforeAction = [this] { juce::ignoreUnused(pluginProcessor.getParameters().copyState()); };
	historyControls.onChange = [this] { syncStageBoxOrder(); refreshPresetLabel(); };
	refreshPresetLabel();
	resized();
	timerCallback();
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
	for (std::size_t position = 0; position < stageButtons.size(); ++position)
	{
		const auto modeIndex = static_cast<std::size_t>(pluginProcessor.getStageOrder()[position]);
		if (&stageButtons[modeIndex] != draggedBox)
			stageButtons[modeIndex].setBounds(layout::margin + static_cast<int>(position) * (layout::stageWidth + layout::stageGap),
				layout::modeTop, layout::stageWidth, layout::modeHeight + 4);
	}
	displayedStageOrder = pluginProcessor.getStageOrder();
}

void PluginEditor::syncStageBoxOrder()
{
	if (displayedStageOrder != pluginProcessor.getStageOrder())
		layoutStageBoxes();
}

void PluginEditor::resized()
{
	ScalableEditor::resized();
	auto& content = getContent();

	const auto contentBounds = content.getLocalBounds();
	presetBrowser.setBounds(24, 80, 800, 480);
	const auto centerArea = contentBounds.withX(layout::margin).withY(layout::headerTop)
		.withWidth(layout::centerWidth).withHeight(layout::bodyHeight);
	const auto bandArea = centerArea.withX(centerArea.getRight() + layout::sectionGap)
		.withWidth(layout::bandWidth);
	const auto rightArea = bandArea.withX(bandArea.getRight() + layout::sectionGap)
		.withWidth(layout::rightWidth);
	primaryPanel.setBounds(centerArea.withHeight(layout::panelHeight));
	shapingPanel.setBounds(centerArea.withY(layout::lowerPanelTop).withHeight(layout::panelHeight));
	bandMixPanel.setBounds(bandArea.withHeight(layout::panelHeight));
	crossoverPanel.setBounds(bandArea.withY(layout::lowerPanelTop).withHeight(layout::panelHeight));
	outputPanel.setBounds(rightArea);

	juce::FlexBox toolbar;
	toolbar.flexDirection = juce::FlexBox::Direction::row;
	toolbar.alignItems = juce::FlexBox::AlignItems::center;
	toolbar.items.add(juce::FlexItem(title).withWidth(156.0f).withHeight(44.0f));
	toolbar.items.add(juce::FlexItem().withFlex(1.0f));
	toolbar.items.add(juce::FlexItem(presetNavigation).withWidth(324.0f).withHeight(44.0f).withMargin({ 0.0f, 12.0f, 0.0f, 0.0f }));
	toolbar.items.add(juce::FlexItem().withFlex(1.0f));
	toolbar.items.add(juce::FlexItem(historyControls).withWidth(152.0f).withHeight(44.0f).withMargin({ 0.0f, 4.0f, 0.0f, 4.0f }));
	toolbar.items.add(juce::FlexItem(settingsButton).withWidth(88.0f).withHeight(44.0f).withMargin({ 0.0f, 8.0f, 0.0f, 8.0f }));
	toolbar.items.add(juce::FlexItem(bypassButton).withWidth(84.0f).withHeight(44.0f));
	toolbar.performLayout(contentBounds.withX(layout::topBarMargin).withY(layout::topBarTop)
		.withWidth(contentBounds.getWidth() - layout::topBarMargin * 2).withHeight(layout::topBarHeight).toFloat());
	modeBox.setBounds({});
	modeBox.setVisible(false);

	stageHeader.setVisible(true);
	stageHeader.setBounds(layout::margin, layout::modeTop - 20, 160, 20);
	layoutStageBoxes();

	const auto layoutRotaryRow = [](auto& controls, std::size_t first, std::size_t count,
		juce::Rectangle<int> area, int cellWidth, ui::RotaryControl::Size size, int valueWidth)
	{
		const auto groupWidth = cellWidth * static_cast<int>(count);
		const auto row = area.withSizeKeepingCentre(groupWidth, layout::primaryControlHeight);
		for (std::size_t index = 0; index < count; ++index)
		{
			auto& control = controls[first + index];
			control.setLayout(size, valueWidth);
			control.setBounds(row.getX() + static_cast<int>(index) * cellWidth + 4,
				row.getY(), cellWidth - 8, row.getHeight());
		}
	};
	using Size = ui::RotaryControl::Size;
	layoutRotaryRow(sliders, 1, 4, primaryPanel.getContentBounds(), 104, Size::standard, 96);
	layoutRotaryRow(macroSliders, 0, 3, shapingPanel.getContentBounds(), 104, Size::standard, 96);
	layoutRotaryRow(bandMixSliders, 0, 3, bandMixPanel.getContentBounds(), 84, Size::compact, 76);
	layoutRotaryRow(cutoffSliders, 0, 2, crossoverPanel.getContentBounds(), 112, Size::compact, 96);

	const auto outputContent = outputPanel.getContentBounds();
	qualityLabel.setJustificationType(juce::Justification::centred);
	qualityLabel.setFont(juce::FontOptions(12.0f));
	qualityLabel.setBounds(outputContent.withY(outputContent.getBottom() - 28).withHeight(28));
	autoGainButton.setBounds(outputContent.withY(outputContent.getBottom() - 72).withHeight(36)
		.withSizeKeepingCentre(132, 36));
	const auto strips = outputContent.withTrimmedBottom(88);
	const auto stripWidth = strips.getWidth() / 2;
	const auto inputStrip = strips.withWidth(stripWidth);
	const auto outputStrip = inputStrip.translated(stripWidth, 0);
	inputLabel.setBounds(inputStrip.withHeight(24));
	outputLabel.setBounds(outputStrip.withHeight(24));
	inputFader.setBounds(inputStrip.withTrimmedTop(32).withWidth(64));
	outputFader.setBounds(outputStrip.withTrimmedTop(32).withWidth(64));
	// Match the fader's visible track, leaving its value field below the meter.
	inputMeter.setBounds(inputFader.getBounds().withTrimmedTop(8).withTrimmedBottom(40)
		.withX(inputStrip.getX() + 68).withWidth(32));
	outputMeter.setBounds(outputFader.getBounds().withTrimmedTop(8).withTrimmedBottom(40)
		.withX(outputStrip.getX() + 68).withWidth(32));

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
	syncStageBoxOrder();
	refreshPresetLabel();
	const auto newInputPeaks = pluginProcessor.consumeInputPeaks();
	const auto newOutputPeaks = pluginProcessor.consumeOutputPeaks();
	inputMeter.setStereoLevels(newInputPeaks);
	outputMeter.setStereoLevels(newOutputPeaks);
	const auto quality = pluginProcessor.getActiveQuality();
	qualityLabel.setText("Quality: " + juce::String(static_cast<int>(quality.multiplier())) + "x " + (quality.filter == dsp::OversamplingFilter::polyphaseFIR ? "FIR" : "IIR") + (pluginProcessor.hasPendingQualityChange() ? " (pending)" : ""), juce::dontSendNotification);
	historyControls.refresh();
	repaint();
}

void PluginEditor::refreshPresetLabel()
{
	const auto index = pluginProcessor.getCurrentPresetIndex();
	const auto& entries = pluginProcessor.getPresetEntries();
	presetNavigation.setPreset(index && *index < entries.size() ? entries[*index].name : "Untitled",
		pluginProcessor.isCurrentPresetModified(), !entries.empty());
}

void PluginEditor::configureRotary(juce::Component& parent, ui::RotaryControl& control, const juce::String& name,
	const char* parameterId, std::unique_ptr<SliderAttachment>& attachment)
{
	control.setLabel(name);
	parent.addAndMakeVisible(control);
	attachment = std::make_unique<SliderAttachment>(pluginProcessor.getParameters(), parameterId, control.getSlider());
	const juce::String id(parameterId);
	auto format = ui::ValueFormat::decimal;
	if (id == parameters::drive) format = ui::ValueFormat::decibels;
	else if (id == parameters::tone) format = ui::ValueFormat::tone;
	else if (id == parameters::mix || id == parameters::lowBandMix
		|| id == parameters::midBandMix || id == parameters::highBandMix)
		format = ui::ValueFormat::percent;
	else if (id == parameters::lowMidCutoffHz || id == parameters::midHighCutoffHz)
		format = ui::ValueFormat::frequency;
	ui::configureValueFormat(control.getSlider(), format);
	control.refreshValueText();
}
}
