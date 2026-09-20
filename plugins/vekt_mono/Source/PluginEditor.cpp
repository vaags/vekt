#include "PluginEditor.h"

namespace vekt::mono
{
PluginEditor::PluginEditor(PluginProcessor& newProcessor)
	: ScalableEditor(newProcessor), pluginProcessor(newProcessor), historyControls(newProcessor.getUndoManager()),
	  presetBrowser(newProcessor.getPresetSession())
{
	setLookAndFeel(&lookAndFeel);
	title.setText("VEKT  MONO", juce::dontSendNotification);
	title.setFont(juce::FontOptions(24.0f).withStyle("Bold"));
	status.setJustificationType(juce::Justification::centredRight);
	historyControls.beforeAction = [this] { juce::ignoreUnused(pluginProcessor.getParameters().copyState()); };
	historyControls.onChange = [this] { timerCallback(); };
	presetNavigation.onBrowse = [this]
	{
		presetBrowser.refresh();
		presetBrowser.setVisible(true);
		presetBrowser.toFront(true);
	};
	presetBrowser.onClose = [this] { presetBrowser.setVisible(false); presetNavigation.focusPreset(); };
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
	for (auto* panel : { &oscillatorPanel, &filterPanel, &voicePanel, &ampPanel, &filterEnvelopePanel, &performancePanel }) getContent().addAndMakeVisible(*panel);
	getContent().addAndMakeVisible(title); getContent().addAndMakeVisible(status);
	getContent().addAndMakeVisible(historyControls);
	getContent().addAndMakeVisible(presetNavigation);
	getContent().addChildComponent(presetBrowser);
	const std::array oscillatorNames { "Osc 1 Level", "Osc 1 Morph", "Osc 1 Width",
		"Osc 2 Level", "Osc 2 Morph", "Osc 2 Width", "Osc 3 Level", "Osc 3 Morph", "Osc 3 Width" };
	const std::array oscillatorIds { parameters::osc1Level, parameters::osc1Morph, parameters::osc1PulseWidth,
		parameters::osc2Level, parameters::osc2Morph, parameters::osc2PulseWidth,
		parameters::osc3Level, parameters::osc3Morph, parameters::osc3PulseWidth };
	for (std::size_t index = 0; index < oscillatorControls.size(); ++index) addRotary(oscillatorPanel, oscillatorControls[index], oscillatorNames[index], oscillatorIds[index], oscillatorAttachments[index]);
	for (const auto index : { std::size_t { 1 }, std::size_t { 4 }, std::size_t { 7 } }) oscillatorControls[index].setWaveformGuide(true);
	const std::array filterNames { "Cutoff", "Resonance", "Key Track", "Env Amount", "Drive" };
	const std::array filterIds { parameters::filterCutoff, parameters::filterResonance, parameters::filterKeyTracking, parameters::filterEnvelopeAmount, parameters::filterDrive };
	for (std::size_t index = 0; index < filterControls.size(); ++index) addRotary(filterPanel, filterControls[index], filterNames[index], filterIds[index], filterAttachments[index]);
	const std::array ampNames { "Attack", "Decay", "Sustain", "Release", "Velocity" };
	const std::array ampIds { parameters::ampAttack, parameters::ampDecay, parameters::ampSustain, parameters::ampRelease, parameters::ampVelocity };
	for (std::size_t index = 0; index < ampControls.size(); ++index) addRotary(ampPanel, ampControls[index], ampNames[index], ampIds[index], ampAttachments[index]);
	const std::array filterEnvelopeIds { parameters::filterAttack, parameters::filterDecay, parameters::filterSustain, parameters::filterRelease, parameters::filterVelocity };
	for (std::size_t index = 0; index < filterEnvelopeControls.size(); ++index) addRotary(filterEnvelopePanel, filterEnvelopeControls[index], ampNames[index], filterEnvelopeIds[index], filterEnvelopeAttachments[index]);
	const std::array voiceNames { "Detune", "Uni Spread", "Voice Width", "Glide Time", "Output" };
	const std::array voiceIds { parameters::unisonDetune, parameters::unisonSpread, parameters::voiceWidth, parameters::glideTime, parameters::masterOutput };
	for (std::size_t index = 0; index < voiceControls.size(); ++index) addRotary(voicePanel, voiceControls[index], voiceNames[index], voiceIds[index], voiceAttachments[index]);
	addChoice(performancePanel, noiseBox, { "Off", "White", "Pink" }, parameters::noiseType, noiseAttachment);
	addChoice(performancePanel, voiceCountBox, { "8", "12", "16" }, parameters::voiceCount, voiceCountAttachment);
	addChoice(performancePanel, performanceModeBox, { "Poly", "Mono", "Mono Legato" }, parameters::performanceMode, performanceModeAttachment);
	addChoice(performancePanel, qualityBox, { "Real-time", "High" }, parameters::quality, qualityAttachment);
	addChoice(performancePanel, unisonBox, { "1x", "2x", "4x" }, parameters::unison, unisonAttachment);
	addChoice(performancePanel, glideBox, { "Off", "Always", "Legato" }, parameters::glideMode, glideAttachment);
	const std::array performanceNames { "Voice count", "Mode", "Quality", "Unison", "Glide", "Noise" };
	for (std::size_t index = 0; index < performanceLabels.size(); ++index)
	{
		performanceLabels[index].setText(performanceNames[index], juce::dontSendNotification);
		performanceLabels[index].setJustificationType(juce::Justification::centredLeft);
		performancePanel.addAndMakeVisible(performanceLabels[index]);
	}
	noiseBox.setTooltip("White or pink noise source.");
	qualityBox.setTooltip("High uses 2x IIR oversampling and adds latency. Changes apply only after transport stops and all notes and sustain are released.");
	voiceCountBox.setTooltip("Voice-count changes apply after all active notes are released.");
	performanceModeBox.setTooltip("Mono uses last-note priority; Mono Legato keeps the envelope active while notes overlap.");
	glideBox.setTooltip("Always glides every note change; Legato glides only while another note is held.");
	refreshPresetLabel();
	resized();
	startTimerHz(10);
}

PluginEditor::~PluginEditor() { setLookAndFeel(nullptr); }
void PluginEditor::addRotary(ui::Panel& panel, ui::RotaryControl& control, const char* name, const char* identifier, std::unique_ptr<SliderAttachment>& attachment)
{
	control.setLabel(name); panel.addAndMakeVisible(control); attachment = std::make_unique<SliderAttachment>(pluginProcessor.getParameters(), identifier, control.getSlider());
}
void PluginEditor::addChoice(ui::Panel& panel, juce::ComboBox& box, const juce::StringArray& choices, const char* identifier, std::unique_ptr<ComboBoxAttachment>& attachment)
{
	box.addItemList(choices, 1); panel.addAndMakeVisible(box); attachment = std::make_unique<ComboBoxAttachment>(pluginProcessor.getParameters(), identifier, box);
}
void PluginEditor::timerCallback()
{
	historyControls.refresh();
	refreshPresetLabel();
	juce::String message = "Quality: " + juce::String(pluginProcessor.getActiveQuality() == 1 ? "High (2x IIR)" : "Real-time")
		+ " • " + juce::String(pluginProcessor.getLatencySamples()) + " smp";
	if (pluginProcessor.hasPendingVoiceCountChange()) message = "Voice count pending—release notes";
	if (pluginProcessor.hasPendingQualityChange()) message += " • Quality pending—stop and release notes";
	status.setText(message, juce::dontSendNotification);
}
void PluginEditor::refreshPresetLabel()
{
	auto& session = pluginProcessor.getPresetSession();
	presetNavigation.setPreset(session.loaded() ? session.loaded()->name : "Untitled", session.modified(),
		!session.library().entries().empty());
}
void PluginEditor::paint(juce::Graphics& graphics) { graphics.fillAll(juce::Colour::fromRGB(20, 24, 28)); }
void PluginEditor::resized()
{
	ScalableEditor::resized(); auto& content = getContent(); title.setBounds(16, 16, 220, 36); presetNavigation.setBounds(244, 16, 300, 36); historyControls.setBounds(552, 16, 104, 36); status.setBounds(664, 16, 360, 36); presetBrowser.setBounds(content.getLocalBounds().reduced(16));
	oscillatorPanel.setBounds(16, 62, 1008, 178); filterPanel.setBounds(16, 254, 488, 170); voicePanel.setBounds(520, 254, 504, 170); ampPanel.setBounds(16, 438, 328, 196); filterEnvelopePanel.setBounds(360, 438, 328, 196); performancePanel.setBounds(704, 438, 320, 196);
	for (std::size_t index = 0; index < oscillatorControls.size(); ++index)
	{
		const auto oscillator = static_cast<int>(index / 3);
		const auto control = static_cast<int>(index % 3);
		oscillatorControls[index].setBounds(8 + oscillator * 336 + control * 108, 28, 104, 135);
	}
	for (std::size_t index = 0; index < filterControls.size(); ++index) filterControls[index].setBounds(4 + static_cast<int>(index) * 96, 28, 92, 130);
	for (std::size_t index = 0; index < ampControls.size(); ++index) ampControls[index].setBounds(4 + static_cast<int>(index) * 64, 28, 64, 135);
	for (std::size_t index = 0; index < filterEnvelopeControls.size(); ++index) filterEnvelopeControls[index].setBounds(4 + static_cast<int>(index) * 64, 28, 64, 135);
	for (std::size_t index = 0; index < voiceControls.size(); ++index) voiceControls[index].setBounds(4 + static_cast<int>(index) * 100, 28, 96, 135);
	voiceCountBox.setBounds(10, 54, 142, 26); performanceModeBox.setBounds(168, 54, 142, 26); qualityBox.setBounds(10, 104, 142, 26); unisonBox.setBounds(168, 104, 142, 26); glideBox.setBounds(10, 154, 142, 26); noiseBox.setBounds(168, 154, 142, 26);
	performanceLabels[0].setBounds(10, 34, 142, 18); performanceLabels[1].setBounds(168, 34, 142, 18); performanceLabels[2].setBounds(10, 84, 142, 18); performanceLabels[3].setBounds(168, 84, 142, 18); performanceLabels[4].setBounds(10, 134, 142, 18); performanceLabels[5].setBounds(168, 134, 142, 18); juce::ignoreUnused(content);
}
}