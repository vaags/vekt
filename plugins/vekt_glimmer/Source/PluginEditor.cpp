#include "PluginEditor.h"

#include <vekt/ui/ValueFormat.h>

namespace vekt::glimmer
{
PluginEditor::PluginEditor(PluginProcessor& newProcessor)
	: ScalableEditor(newProcessor), pluginProcessor(newProcessor),
	  historyControls(newProcessor.getUndoManager()),
	  presetBrowser(newProcessor.getPresetSession())
{
	setLookAndFeel(&lookAndFeel);
	title.setText("VEKT  GLIMMER", juce::dontSendNotification);
	title.setFont(juce::FontOptions(24.0f).withStyle("Bold"));
	modelHeader.setText("Model", juce::dontSendNotification);
	historyControls.beforeAction = [this] { juce::ignoreUnused(pluginProcessor.getParameters().copyState()); };
	historyControls.onChange = [this] { timerCallback(); };
	presetNavigation.onBrowse = [this]
	{
		presetBrowser.refresh();
		presetBrowser.setVisible(true);
		presetBrowser.toFront(true);
	};
	presetBrowser.onClose = [this]
	{
		presetBrowser.setVisible(false);
		presetNavigation.focusPreset();
	};
	presetBrowser.onSoundChanged = [this] { refreshPresetLabel(); };
	const auto reportLoad = [this](const juce::Result& result)
	{
		if (result.failed())
		{
			presetBrowser.refresh();
			presetBrowser.setVisible(true);
			presetBrowser.toFront(true);
			presetBrowser.showResult(result);
		}
		refreshPresetLabel();
	};
	presetNavigation.onPrevious = [this, reportLoad] { reportLoad(pluginProcessor.loadPreviousPreset()); };
	presetNavigation.onNext = [this, reportLoad] { reportLoad(pluginProcessor.loadNextPreset()); };
	autoTargetLabel.setJustificationType(juce::Justification::centred);
	for (auto* panel : { &rotationPanel, &microphonePanel, &tonePanel, &ioPanel })
		getContent().addAndMakeVisible(*panel);
	getContent().addAndMakeVisible(title);
	getContent().addAndMakeVisible(modelHeader);
	getContent().addAndMakeVisible(historyControls);
	getContent().addAndMakeVisible(presetNavigation);
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
	constexpr std::array toneNames { "Horn Tone", "Drum Tone", "Preamp", "Mix" };
	constexpr std::array toneIds { parameters::hornTone, parameters::drumTone, parameters::preampDrive, parameters::mix };
	for (std::size_t index = 0; index < toneControls.size(); ++index)
		configureRotary(tonePanel, toneControls[index], toneNames[index], toneIds[index], toneAttachments[index]);

	speedModeBox.addItemList({ "Slow", "Fast", "Auto" }, 1);
	rotationPanel.addAndMakeVisible(speedModeBox);
	speedModeAttachment = std::make_unique<ComboBoxAttachment>(pluginProcessor.getParameters(), parameters::speedMode, speedModeBox);
	const std::array<juce::String, 3> modelNames { "Classic", "Drum", "Wide" };
	for (std::size_t index = 0; index < modelButtons.size(); ++index)
	{
		auto& button = modelButtons[index];
		button.setButtonText(modelNames[index]);
		button.setName(modelNames[index] + " model");
		button.setTooltip(modelNames[index] + " rotary speaker model");
		button.setClickingTogglesState(true);
		button.setRadioGroupId(701);
		getContent().addAndMakeVisible(button);
		button.onClick = [this, index]
		{
			modelAttachment->setValueAsCompleteGesture(static_cast<float>(index));
			refreshPresetLabel();
		};
	}
	modelAttachment = std::make_unique<juce::ParameterAttachment>(
		*pluginProcessor.getParameters().getParameter(parameters::cabinetModel), [this](float value)
		{
			for (std::size_t index = 0; index < modelButtons.size(); ++index)
				modelButtons[index].setToggleState(static_cast<int>(index) == juce::roundToInt(value), juce::dontSendNotification);
		}, &pluginProcessor.getUndoManager());
	modelAttachment->sendInitialUpdate();
	for (auto* component : { static_cast<juce::Component*>(&brakeButton), static_cast<juce::Component*>(&manualButton),
		static_cast<juce::Component*>(&speedSlider), static_cast<juce::Component*>(&speedLabel) })
		rotationPanel.addAndMakeVisible(*component);
	microphonePanel.addAndMakeVisible(widthSlider);
	microphonePanel.addAndMakeVisible(widthLabel);
	speedLabel.setText("Speed", juce::dontSendNotification);
	widthLabel.setText("Width", juce::dontSendNotification);
	speedSlider.setName("Manual speed position");
	widthSlider.setName("Wet stereo width");
	brakeButton.setName("Brake");
	manualButton.setName("Manual speed override");
	brakeButton.setTooltip("Decelerate to a stop; release to resume the selected speed");
	manualButton.setTooltip("Override Slow/Fast/Auto with the continuous Speed control");
	speedSlider.setTooltip("Continuous position between Slow and Fast, with rotor inertia");
	widthSlider.setTooltip("Wet stereo spread: 100% unchanged, 0% mono, above 100% requires headroom");
	for (auto* slider : { &speedSlider, &widthSlider })
	{
		slider->setSliderStyle(juce::Slider::LinearHorizontal);
		slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 24);
		slider->setTextValueSuffix(" %");
	}
	speedSlider.setDoubleClickReturnValue(true, 0);
	widthSlider.setDoubleClickReturnValue(true, 100);
	brakeAttachment = std::make_unique<ButtonAttachment>(pluginProcessor.getParameters(), parameters::brake, brakeButton);
	manualAttachment = std::make_unique<ButtonAttachment>(pluginProcessor.getParameters(), parameters::manualSpeedEnabled, manualButton);
	speedAttachment = std::make_unique<SliderAttachment>(pluginProcessor.getParameters(), parameters::speedPosition, speedSlider);
	widthAttachment = std::make_unique<SliderAttachment>(pluginProcessor.getParameters(), parameters::stereoWidth, widthSlider);
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
	timerCallback();
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
	const juce::String identifier(parameter);
	if (identifier == parameters::hornTone || identifier == parameters::drumTone || identifier == parameters::preampDrive)
		ui::configureValueFormat(control.getSlider(), ui::ValueFormat::decibels);
	else if (identifier == parameters::mix || identifier == parameters::sensitivity || identifier == parameters::hornDrumBalance)
		ui::configureValueFormat(control.getSlider(), ui::ValueFormat::percent);
	else
		ui::configureValueFormat(control.getSlider(), ui::ValueFormat::decimal);
	control.refreshValueText();
}

void PluginEditor::refreshPresetLabel()
{
	const auto& session = pluginProcessor.getPresetSession();
	const auto available = !pluginProcessor.getPresetSession().library().entries().empty();
	presetNavigation.setPreset(session.loaded() ? session.loaded()->name : "Untitled", session.modified(), available);
}

void PluginEditor::timerCallback()
{
	historyControls.refresh();
	refreshPresetLabel();
	inputMeter.setStereoLevels(pluginProcessor.consumeInputPeaks());
	outputMeter.setStereoLevels(pluginProcessor.consumeOutputPeaks());
	const auto& state = pluginProcessor.getParameters();
	const auto manual = state.getRawParameterValue(parameters::manualSpeedEnabled)->load() >= 0.5f;
	const auto braking = state.getRawParameterValue(parameters::brake)->load() >= 0.5f;
	const auto mode = juce::roundToInt(state.getRawParameterValue(parameters::speedMode)->load());
	const auto drumOnly = juce::roundToInt(state.getRawParameterValue(parameters::cabinetModel)->load()) == 1;
	rotationControls[4].setEnabled(mode == 2 && !manual && !braking);
	speedSlider.setEnabled(manual && !braking);
	speedModeBox.setEnabled(!manual && !braking);
	microphoneControls[0].setEnabled(!drumOnly);
	toneControls[0].setEnabled(!drumOnly);
	const auto speeds = pluginProcessor.getRotorSpeeds();
	juce::String status = braking ? (std::abs(speeds[1]) < 0.1f && (drumOnly || std::abs(speeds[0]) < 0.1f) ? "Stopped" : "Braking")
		: manual ? "Manual" : mode == 2 ? (pluginProcessor.isAutoTargetFast() ? "Auto: Fast" : "Auto: Slow") : mode == 1 ? "Fast" : "Slow";
	if (pluginProcessor.hasPendingModelChange())
	{
		const std::array<juce::String, 3> names { "Classic", "Drum", "Wide" };
		status = names[static_cast<std::size_t>(pluginProcessor.getActiveModel())] + " -> " +
			names[static_cast<std::size_t>(juce::roundToInt(state.getRawParameterValue(parameters::cabinetModel)->load()))];
	}
	status += drumOnly ? "\nD " + juce::String(std::abs(speeds[1]), 0) + " rpm"
		: "\nH " + juce::String(std::abs(speeds[0]), 0) + " / D " + juce::String(std::abs(speeds[1]), 0) + " rpm";
	autoTargetLabel.setText(status, juce::dontSendNotification);
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
	title.setBounds(20, 16, 224, 40);
	presetNavigation.setBounds(280, 16, 320, 44);
	historyControls.setBounds(664, 16, 160, 44);
	modelHeader.setBounds(20, 66, 160, 20);
	for (std::size_t index = 0; index < modelButtons.size(); ++index)
		modelButtons[index].setBounds(20 + static_cast<int>(index) * 364, 86, 352, 48);
	autoTargetLabel.setBounds(900, 10, 200, 52);
	presetBrowser.setBounds(content.getLocalBounds().reduced(20));
	rotationPanel.setBounds(20, 150, 710, 238);
	microphonePanel.setBounds(746, 150, 354, 238);
	tonePanel.setBounds(20, 404, 710, 276);
	ioPanel.setBounds(746, 404, 354, 276);
	for (std::size_t index = 0; index < rotationControls.size(); ++index)
		rotationControls[index].setBounds(10 + static_cast<int>(index) * 138, 32, 130, 146);
	speedModeBox.setBounds(18, 190, 126, 30);
	manualButton.setBounds(156, 190, 86, 30);
	speedLabel.setBounds(254, 190, 48, 30);
	speedSlider.setBounds(302, 190, 278, 30);
	brakeButton.setBounds(594, 190, 98, 30);
	for (std::size_t index = 0; index < microphoneControls.size(); ++index)
		microphoneControls[index].setBounds(14 + static_cast<int>(index) * 108, 32, 102, 146);
	widthLabel.setBounds(14, 190, 48, 30);
	widthSlider.setBounds(66, 190, 274, 30);
	for (std::size_t index = 0; index < toneControls.size(); ++index)
		toneControls[index].setBounds(10 + static_cast<int>(index) * 174, 48, 166, 176);
	inputFader.setBounds(26, 38, 96, 138);
	outputFader.setBounds(140, 38, 96, 138);
	inputMeter.setBounds(254, 42, 30, 130);
	outputMeter.setBounds(298, 42, 30, 130);
	bypassButton.setBounds(28, 184, 104, 26);
	autoGainButton.setBounds(146, 184, 116, 26);
	trackingQualityBox.setBounds(18, 226, 152, 24);
	offlineQualityBox.setBounds(184, 226, 152, 24);
	qualityLabel.setBounds(18, 4, 318, 24);
	juce::ignoreUnused(content);
}
}
