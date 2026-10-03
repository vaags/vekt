#include <vekt/flint/PluginEditor.h>

#include "DriveStage.h"
#include "KickClassicAnalog.h"
#include "MalletBar.h"

#include <vekt/dsp/OversamplingChoices.h>
#include <vekt/ui/ValueFormat.h>

#include <algorithm>
#include <cmath>

namespace vekt::flint
{
namespace
{
constexpr int modeRadioGroup = 801;
constexpr int modelRadioGroup = 802;
constexpr int driveTypeRadioGroup = 803;
constexpr std::array driveTypeNames { "Soft", "Hard", "Fold" };

juce::String formatSeconds(double seconds)
{
	if (seconds < 0.01) return juce::String(seconds * 1'000.0, 2) + " ms";
	if (seconds < 1.0) return juce::String(juce::roundToInt(seconds * 1'000.0)) + " ms";
	return juce::String(seconds, seconds < 10.0 ? 2 : 1) + " s";
}

juce::String formatHertz(double hertz)
{
	return hertz >= 1'000.0 ? juce::String(hertz / 1'000.0, 2) + " kHz" : juce::String(hertz, 1) + " Hz";
}

double noteHertz(double note) { return 440.0 * std::exp2((note - 69.0) / 12.0); }

// Pitch entry in the editor: a note with optional cents ("D#2", "D#2 +12 ct"), a frequency ("77.8 Hz", "1.2 kHz") or a
// MIDI note number. Text after the display's separator is ignored, so an edited readout still parses.
std::optional<double> parsePitch(juce::String text)
{
	text = text.upToFirstOccurrenceOf(juce::String::fromUTF8("\xc2\xb7"), false, false).trim();
	if (text.isEmpty()) return std::nullopt;
	const auto lower = text.toLowerCase();
	if (lower.endsWith("hz"))
	{
		const auto format = ui::ValueFormat::frequency;
		const auto hertz = ui::parseValue(text, format);
		if (!hertz.has_value() || *hertz <= 0.0) return std::nullopt;
		return 69.0 + 12.0 * std::log2(*hertz / 440.0);
	}
	if (const auto number = ui::parseValue(text, ui::ValueFormat::decimal); number.has_value()) return number;
	static const juce::StringArray names { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
	const auto token = text.upToFirstOccurrenceOf(" ", false, false).toUpperCase();
	const auto hasSharp = token.length() > 1 && token[1] == '#';
	const auto index = names.indexOf(token.substring(0, hasSharp ? 2 : 1));
	const auto octaveText = token.substring(hasSharp ? 2 : 1);
	if (index < 0 || octaveText.isEmpty() || !octaveText.trimCharactersAtStart("-").containsOnly("0123456789"))
		return std::nullopt;
	auto note = static_cast<double>(12 * (octaveText.getIntValue() + 1) + index);
	const auto rest = text.fromFirstOccurrenceOf(" ", false, false).trim();
	if (rest.isNotEmpty())
	{
		const auto cents = rest.upToFirstOccurrenceOf("ct", false, true).trim();
		if (cents.isEmpty() || !cents.containsOnly("0123456789+-.")) return std::nullopt;
		note += cents.getDoubleValue() / 100.0;
	}
	return note;
}
}

PluginEditor::PluginEditor(PluginProcessor& newProcessor)
    : ScalableEditor(newProcessor), pluginProcessor(newProcessor), historyControls(newProcessor.getUndoManager()),
      presetBrowser(newProcessor.getPresetSession()),
      settings(newProcessor.getParameters(), parameters::trackingOversampling, parameters::offlineOversampling)
{
	setLookAndFeel(&lookAndFeel);
	auto& state = pluginProcessor.getParameters();
	auto& content = getContent();
	title.setText("FLINT", juce::dontSendNotification);
	title.setFont(juce::FontOptions(24.0f).withStyle("Bold"));
	content.addAndMakeVisible(title);

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
	content.addAndMakeVisible(historyControls);
	content.addAndMakeVisible(presetNavigation);

	// Mode row: a Mode change also sets the shared controls to the mode's start values, as one undo step (A31).
	for (std::size_t index = 0; index < modeButtons.size(); ++index)
	{
		auto& button = modeButtons[index];
		const auto mode = static_cast<Mode>(index);
		const juce::String name = modeNames[index];
		button.setButtonText(name);
		button.setName(name + " mode");
		button.setRadioGroupId(modeRadioGroup);
		button.setEnabled(isAvailable(mode));
		button.setTooltip(isAvailable(mode) ? name + ": sets the shared controls to its starting sound"
		                                    : name + " is not available in this version");
		button.onClick = [this, mode]
		{
			if (mode != selectedMode()) pluginProcessor.selectMode(mode);
			refreshSelection();
		};
		content.addAndMakeVisible(button);
	}
	for (std::size_t index = 0; index < modelButtons.size(); ++index)
	{
		auto& button = modelButtons[index];
		button.setRadioGroupId(modelRadioGroup);
		button.onClick = [this, index]
		{
			if (selectedMode() == Mode::kick) kickModelAttachment->setValueAsCompleteGesture(static_cast<float>(index));
			if (selectedMode() == Mode::mallet)
				malletModelAttachment->setValueAsCompleteGesture(static_cast<float>(index));
			refreshSelection();
		};
		content.addChildComponent(button);
	}
	const auto attachSelection = [this, &state](const char* identifier)
	{
		auto attachment = std::make_unique<juce::ParameterAttachment>(
		    *state.getParameter(identifier), [this](float) { refreshSelection(); }, &pluginProcessor.getUndoManager());
		return attachment;
	};
	modeAttachment = attachSelection(parameters::mode);
	kickModelAttachment = attachSelection(parameters::kickModel);
	malletModelAttachment = attachSelection(parameters::malletModel);

	for (auto* panel : { &soundPanel, &modelPanel, &feelPanel, &ioPanel }) content.addAndMakeVisible(*panel);

	constexpr std::array sharedNames { "Pitch", "Attack", "Decay", "Tone", "Drive" };
	constexpr std::array sharedIds { parameters::pitch, parameters::attack, parameters::decay, parameters::tone,
		parameters::drive };
	for (std::size_t index = 0; index < sharedControls.size(); ++index)
	{
		configureRotary(
		    soundPanel, sharedControls[index], sharedNames[index], sharedIds[index], sharedAttachments[index]);
		sharedDetails[index].setName(juce::String(sharedNames[index]) + " detail");
		sharedDetails[index].setJustificationType(juce::Justification::centred);
		sharedDetails[index].setFont(juce::FontOptions(13.0f));
		soundPanel.addAndMakeVisible(sharedDetails[index]);
	}
	auto& pitch = sharedControls[0];
	pitch.setLayout(ui::RotaryControl::Size::large, 196);
	auto& pitchSlider = pitch.getSlider();
	pitchSlider.textFromValueFunction = [this](double value) { return pitchDisplay(value); };
	pitchSlider.valueFromTextFunction = [this, &pitchSlider](const juce::String& text)
	{
		pitchSlider.getProperties().set("valueEntryError", false);
		// Committing the unchanged readout keeps the exact value.
		if (text.trim() == pitchDisplay(pitchSlider.getValue())) return pitchSlider.getValue();
		const auto parsed = parsePitch(text);
		pitchSlider.getProperties().set("valueEntryError", !parsed.has_value());
		return parsed.has_value() ? std::clamp(*parsed, fullPitchRange.lowest, fullPitchRange.highest)
		                          : pitchSlider.getValue();
	};
	// Dragging snaps to semitones; Shift-drag is continuous. Host automation and text entry stay continuous.
	pitch.setDragSnap(
	    [](double value) { return juce::ModifierKeys::currentModifiers.isShiftDown() ? value : std::round(value); });
	pitchSlider.setTooltip("Pitch: drag snaps to semitones, Shift-drag is continuous. Enter a note (\"D#2 +12 ct\"), "
	                       "a frequency (\"78 Hz\") or a MIDI note number.");

	for (std::size_t index = 0; index < driveTypeButtons.size(); ++index)
	{
		auto& button = driveTypeButtons[index];
		button.setButtonText(driveTypeNames[index]);
		button.setName(juce::String(driveTypeNames[index]) + " drive");
		button.setTooltip("Drive Type: " + juce::String(driveTypeNames[index]));
		button.setClickingTogglesState(true);
		button.setRadioGroupId(driveTypeRadioGroup);
		button.setConnectedEdges((index > 0 ? juce::Button::ConnectedOnLeft : 0) |
		    (index + 1 < driveTypeButtons.size() ? juce::Button::ConnectedOnRight : 0));
		button.onClick = [this, index]
		{
			if (driveTypeButtons[index].getToggleState())
				driveTypeAttachment->setValueAsCompleteGesture(static_cast<float>(index));
		};
		soundPanel.addAndMakeVisible(button);
	}
	driveTypeAttachment = std::make_unique<juce::ParameterAttachment>(
	    *state.getParameter(parameters::driveType),
	    [this](float value)
	    {
		    for (std::size_t index = 0; index < driveTypeButtons.size(); ++index)
			    driveTypeButtons[index].setToggleState(
			        static_cast<int>(index) == juce::roundToInt(value), juce::dontSendNotification);
	    },
	    &pluginProcessor.getUndoManager());
	driveTypeAttachment->sendInitialUpdate();

	for (std::size_t index = 0; index < modelControls.size(); ++index)
	{
		modelPanel.addChildComponent(modelControls[index]);
		modelControls[index].setLayout(ui::RotaryControl::Size::standard, 84);
		modelDetails[index].setJustificationType(juce::Justification::centred);
		modelDetails[index].setFont(juce::FontOptions(13.0f));
		modelPanel.addChildComponent(modelDetails[index]);
	}
	configureRotary(feelPanel, feelControls[0], "Velocity", parameters::velocity, feelAttachments[0]);
	configureRotary(feelPanel, feelControls[1], "Variation", parameters::variation, feelAttachments[1]);
	feelControls[0].getSlider().setTooltip("Velocity: how strongly note velocity sets the strike's strength");
	feelControls[1].getSlider().setTooltip(
	    "Variation: 0 % every hit identical, about 30 % natural, 100 % loose. Hits repeat with the song position.");

	// Settings: quality, Note Off Damps and New Seed in the shared anchored pop-over.
	settings.setTitle("Settings");
	content.addAndMakeVisible(settings.getSettingsButton());
	content.addChildComponent(settings);
	noteOffDampsButton.setName("Note Off Damps");
	noteOffDampsButton.setTooltip("Note Off shortens the ringing tail");
	newSeedButton.setName("New Seed");
	newSeedButton.setTooltip("Draw a new random seed: every hit's variation changes. Saved with the project, not in "
	                         "presets.");
	newSeedButton.onClick = [this] { pluginProcessor.newSeed(); };
	settings.addAndMakeVisible(noteOffDampsButton);
	settings.addAndMakeVisible(newSeedButton);
	noteOffDampsAttachment = std::make_unique<ButtonAttachment>(state, parameters::noteOffDamps, noteOffDampsButton);

	qualityLabel.setName("Active quality");
	qualityLabel.setJustificationType(juce::Justification::centred);
	levelFader.setName("Level");
	levelFader.setTooltip("Level: the instrument's output level");
	levelFader.setSliderStyle(juce::Slider::LinearVertical);
	levelFader.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 72, 24);
	levelFader.setColour(juce::Slider::trackColourId, juce::Colour::fromRGB(227, 156, 75));
	levelAttachment = std::make_unique<SliderAttachment>(state, parameters::level, levelFader);
	levelFader.setDoubleClickReturnValue(true, 0.0);
	ui::configureValueFormat(levelFader, ui::ValueFormat::decibels);
	outputScope.setName("Output scope");
	for (auto* component : { static_cast<juce::Component*>(&qualityLabel), static_cast<juce::Component*>(&levelFader),
	         static_cast<juce::Component*>(&outputMeter), static_cast<juce::Component*>(&outputScope) })
		ioPanel.addAndMakeVisible(*component);
	content.addChildComponent(presetBrowser);

	for (auto& control : sharedControls) control.getSlider().addListener(this);
	for (auto& control : modelControls) control.getSlider().addListener(this);
	refreshSelection();
	resized();
	timerCallback();
	startTimerHz(30);
}

PluginEditor::~PluginEditor()
{
	for (auto& control : sharedControls) control.getSlider().removeListener(this);
	for (auto& control : modelControls) control.getSlider().removeListener(this);
	setLookAndFeel(nullptr);
}

void PluginEditor::configureRotary(juce::Component& parent, ui::RotaryControl& control, const char* name,
    const char* parameter, std::unique_ptr<SliderAttachment>& attachment)
{
	control.setLabel(name);
	control.setLayout(ui::RotaryControl::Size::standard, 84);
	parent.addAndMakeVisible(control);
	attachment = std::make_unique<SliderAttachment>(pluginProcessor.getParameters(), parameter, control.getSlider());
	if (juce::String(parameter) != parameters::pitch)
		ui::configureValueFormat(control.getSlider(), ui::ValueFormat::percent);
	control.refreshValueText();
}

Mode PluginEditor::selectedMode() const noexcept
{
	const auto index =
	    std::clamp(juce::roundToInt(parameterValue(parameters::mode)), 0, static_cast<int>(modeCount) - 1);
	return static_cast<Mode>(index);
}

std::optional<ModelId> PluginEditor::selectedModel() const noexcept
{
	const auto* identifier = parameters::modelParameterOf(selectedMode());
	return modelAt(selectedMode(), identifier != nullptr ? juce::roundToInt(parameterValue(identifier)) : 0);
}

float PluginEditor::parameterValue(const char* identifier) const noexcept
{
	// The parameter's own value: attachment callbacks run before the state's raw value has caught up.
	const auto* parameter = pluginProcessor.getParameters().getParameter(identifier);
	return parameter->convertFrom0to1(parameter->getValue());
}

juce::String PluginEditor::pitchDisplay(double note) const
{
	const auto range = boundModel.has_value() ? pitchRangeOf(*boundModel) : fullPitchRange;
	const auto played = std::clamp(note, range.lowest, range.highest);
	return parameters::pitchText(static_cast<float>(played)) + juce::String::fromUTF8(" \xc2\xb7 ") +
	    formatHertz(noteHertz(played));
}

void PluginEditor::refreshSelection()
{
	const auto mode = selectedMode();
	for (std::size_t index = 0; index < modeButtons.size(); ++index)
		modeButtons[index].setToggleState(static_cast<Mode>(index) == mode, juce::dontSendNotification);
	const auto models = modelsOf(mode);
	const auto* identifier = parameters::modelParameterOf(mode);
	const auto selectedIndex = identifier != nullptr ? juce::roundToInt(parameterValue(identifier)) : 0;
	for (std::size_t index = 0; index < modelButtons.size(); ++index)
	{
		auto& button = modelButtons[index];
		const auto shown = index < models.size();
		button.setVisible(shown);
		if (!shown) continue;
		const auto& entry = models[index];
		const juce::String name = entry.name;
		button.setButtonText(name);
		button.setName(name + " model");
		button.setEnabled(isAvailable(entry.id) && identifier != nullptr);
		button.setTooltip(isAvailable(entry.id) ? name : name + " is not available in this version");
		button.setToggleState(static_cast<int>(index) == selectedIndex, juce::dontSendNotification);
	}
	bindModelControls(selectedModel());
}

void PluginEditor::bindModelControls(std::optional<ModelId> model)
{
	if (modelBound && model == boundModel) return;
	modelBound = true;
	boundModel = model;
	const auto controls =
	    model.has_value() ? parameters::modelControlsOf(*model) : std::span<const parameters::ModelControl> {};
	for (std::size_t index = 0; index < modelControls.size(); ++index)
	{
		modelAttachments[index].reset();
		const auto shown = index < controls.size();
		modelControls[index].setVisible(shown);
		modelDetails[index].setVisible(shown);
		if (!shown) continue;
		const auto& control = controls[index];
		modelControls[index].setLabel(control.name);
		modelDetails[index].setName(juce::String(control.name) + " detail");
		modelAttachments[index] = std::make_unique<SliderAttachment>(
		    pluginProcessor.getParameters(), control.identifier, modelControls[index].getSlider());
		// Listeners run oldest first: listen again after the new attachment, so the readouts see the parameter it set.
		modelControls[index].getSlider().removeListener(this);
		modelControls[index].getSlider().addListener(this);
		ui::configureValueFormat(modelControls[index].getSlider(), ui::ValueFormat::percent);
		modelControls[index].refreshValueText();
	}
	juce::String modelName { "Model" };
	for (const auto& entry : modelsOf(selectedMode()))
		if (entry.id == model) modelName = entry.name;
	modelPanel.setTitle(modelName);
	auto& pitchSlider = sharedControls[0].getSlider();
	const auto range = model.has_value() ? pitchRangeOf(*model) : fullPitchRange;
	pitchSlider.getProperties().set("playableFrom", range.lowest);
	pitchSlider.getProperties().set("playableTo", range.highest);
	pitchSlider.repaint();
	sharedControls[0].refreshValueText();
	refreshDetails();
}

void PluginEditor::refreshDetails()
{
	const auto pitch = static_cast<double>(parameterValue(parameters::pitch));
	const auto attack = static_cast<double>(parameterValue(parameters::attack)) / 100.0;
	const auto decay = static_cast<double>(parameterValue(parameters::decay)) / 100.0;
	const auto tone = static_cast<double>(parameterValue(parameters::tone)) / 100.0;
	const auto drive = static_cast<double>(parameterValue(parameters::drive)) / 100.0;
	std::array<juce::String, 5> shared;
	std::array<juce::String, parameters::maximumModelControls> own;
	if (boundModel.has_value())
	{
		const auto range = pitchRangeOf(*boundModel);
		if (pitch < range.lowest || pitch > range.highest)
			shared[0] = (pitch < range.lowest ? "below range: " : "above range: ") +
			    parameters::pitchText(static_cast<float>(pitch));
	}
	shared[4] = drive > 0.0
	    ? "+" + juce::String(juce::Decibels::gainToDecibels(DriveStage::inputGain(drive)), 1) + " dB"
	    : juce::String("off");
	if (boundModel == ModelId::kickClassicAnalog)
	{
		shared[1] = "pulse " + formatSeconds(KickClassicAnalog::pulseFallSeconds(attack));
		shared[2] = "T60 " + formatSeconds(KickClassicAnalog::t60Seconds(decay));
		shared[3] = "cutoff " + formatHertz(KickClassicAnalog::toneCutoffHz(tone));
		const auto sweep = static_cast<double>(parameterValue(parameters::kickSweep)) / 100.0;
		own[0] =
		    sweep > 0.0 ? "+" + juce::String(KickClassicAnalog::sweepSemitones(sweep), 1) + " st" : juce::String("off");
		own[1] = formatSeconds(KickClassicAnalog::sweepTimeSeconds(
		    static_cast<double>(parameterValue(parameters::kickSweepTime)) / 100.0));
	}
	else if (boundModel == ModelId::malletBar)
	{
		const auto hardness = static_cast<double>(parameterValue(parameters::barHardness)) / 100.0;
		const auto contact =
		    std::min(MalletBar::maximumContactSeconds, MalletBar::strikeContactSeconds(hardness, attack, 1.0));
		shared[1] = "contact " + formatSeconds(contact);
		shared[2] = "T60 " + formatSeconds(MalletBar::fundamentalT60(decay, MalletBar::pitchHz(pitch)));
		const auto tilt = 24.0 * (tone - 0.5);
		shared[3] = (tilt > 0.0 ? "+" : "") + juce::String(tilt, 1) + " dB/oct";
		own[0] = parameterValue(parameters::barMaterial) < 50.0f ? "wood" : "metal";
		own[1] = "contact " + formatSeconds(MalletBar::contactSeconds(hardness));
		const auto overtones = parameterValue(parameters::barOvertones);
		own[3] = overtones < 0.5f ? "Bar"
		    : overtones < 49.5f   ? "Bar to Xylo"
		    : overtones <= 50.5f  ? "Xylo"
		    : overtones < 99.5f   ? "Xylo to Marimba"
		                          : "Marimba";
	}
	for (std::size_t index = 0; index < sharedDetails.size(); ++index)
		sharedDetails[index].setText(shared[index], juce::dontSendNotification);
	for (std::size_t index = 0; index < modelDetails.size(); ++index)
		modelDetails[index].setText(own[index], juce::dontSendNotification);
}

void PluginEditor::refreshPresetLabel()
{
	auto& session = pluginProcessor.getPresetSession();
	const auto available = !session.library().entries().empty();
	presetNavigation.setPreset(session.loaded() ? session.loaded()->name : "Untitled", session.modified(), available);
}

void PluginEditor::timerCallback()
{
	historyControls.refresh();
	refreshPresetLabel();
	outputMeter.setStereoLevels(pluginProcessor.consumeOutputPeaks());
	qualityLabel.setText(
	    "Quality: " + dsp::qualityName(pluginProcessor.getActiveQuality()), juce::dontSendNotification);
	refreshDetails();
}

void PluginEditor::paint(juce::Graphics& graphics)
{
	graphics.fillAll(lookAndFeel.findColour(juce::ResizableWindow::backgroundColourId));
}

void PluginEditor::resized()
{
	ScalableEditor::resized();
	auto& content = getContent();
	// Command bar.
	title.setBounds(20, 16, 160, 40);
	presetNavigation.setBounds(200, 16, 320, 44);
	historyControls.setBounds(536, 16, 120, 44);
	settings.getSettingsButton().setBounds(672, 18, 88, 36);
	settings.setBounds(672, 64, ui::QualitySettings::preferredWidth, ui::QualitySettings::preferredHeight);
	const auto settingsContent = settings.getContentBounds();
	noteOffDampsButton.setBounds(settingsContent.getX(), settingsContent.getY() + 134, 170, 30);
	newSeedButton.setBounds(settingsContent.getRight() - 140, settingsContent.getY() + 134, 140, 30);
	presetBrowser.setBounds(content.getLocalBounds().reduced(20));

	// Mode and Model rows.
	for (std::size_t index = 0; index < modeButtons.size(); ++index)
		modeButtons[index].setBounds(20 + static_cast<int>(index) * 121, 72, 112, 48);
	for (std::size_t index = 0; index < modelButtons.size(); ++index)
		modelButtons[index].setBounds(20 + static_cast<int>(index) * 218, 128, 208, 44);

	// Sound: the shared controls, dial centres and readouts aligned whatever the dial size.
	soundPanel.setBounds(20, 186, 830, 260);
	constexpr std::array sharedX { 14, 232, 376, 520, 664 };
	constexpr std::array sharedWidth { 204, 136, 136, 136, 150 };
	for (std::size_t index = 0; index < sharedControls.size(); ++index)
	{
		sharedControls[index].setBounds(sharedX[index], 28, sharedWidth[index], 166);
		sharedDetails[index].setBounds(sharedX[index], 194, sharedWidth[index], 20);
	}
	// Drive Type sits under Drive.
	for (std::size_t index = 0; index < driveTypeButtons.size(); ++index)
		driveTypeButtons[index].setBounds(sharedX[4] + static_cast<int>(index) * 50, 220, 50, 26);

	modelPanel.setBounds(20, 462, 580, 218);
	for (std::size_t index = 0; index < modelControls.size(); ++index)
	{
		modelControls[index].setBounds(14 + static_cast<int>(index) * 112, 30, 108, 146);
		modelDetails[index].setBounds(14 + static_cast<int>(index) * 112, 178, 108, 20);
	}
	feelPanel.setBounds(616, 462, 234, 218);
	for (std::size_t index = 0; index < feelControls.size(); ++index)
		feelControls[index].setBounds(10 + static_cast<int>(index) * 108, 30, 106, 146);

	// I/O: active quality, scope, then the Level fader beside the output meter.
	ioPanel.setBounds(866, 186, 234, 494);
	qualityLabel.setBounds(12, 36, 210, 24);
	outputScope.setBounds(12, 66, 210, 150);
	levelFader.setBounds(40, 232, 80, 240);
	outputMeter.setBounds(140, 240, 40, 196);
}
}
