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
	for (auto* panel : { &oscillatorPanel, &filterPanel, &voicePanel, &ioPanel, &ampPanel, &filterEnvelopePanel, &performancePanel }) getContent().addAndMakeVisible(*panel);
	getContent().addAndMakeVisible(title); getContent().addAndMakeVisible(status);
	getContent().addAndMakeVisible(historyControls);
	getContent().addAndMakeVisible(presetNavigation);
	getContent().addChildComponent(presetBrowser);
	const std::array oscillatorNames { "O1 Level", "O1 Morph", "O1 Width", "O1 Octave", "O1 Fine",
		"O2 Level", "O2 Morph", "O2 Width", "O2 Octave", "O2 Fine",
		"O3 Level", "O3 Morph", "O3 Width", "O3 Octave", "O3 Fine", "Noise Level" };
	const std::array oscillatorIds { parameters::osc1Level, parameters::osc1Morph, parameters::osc1PulseWidth, parameters::osc1Octave, parameters::osc1Fine,
		parameters::osc2Level, parameters::osc2Morph, parameters::osc2PulseWidth, parameters::osc2Octave, parameters::osc2Fine,
		parameters::osc3Level, parameters::osc3Morph, parameters::osc3PulseWidth, parameters::osc3Octave, parameters::osc3Fine, parameters::noiseLevel };
	for (std::size_t index = 0; index < oscillatorControls.size(); ++index) addRotary(oscillatorPanel, oscillatorControls[index], oscillatorNames[index], oscillatorIds[index], oscillatorAttachments[index]);
	for (auto& control : oscillatorControls) control.setLayout(ui::RotaryControl::Size::compact, 62);
	for (const auto index : { std::size_t { 1 }, std::size_t { 6 }, std::size_t { 11 } }) oscillatorControls[index].setWaveformGuide(true);
	for (const auto index : { std::size_t { 3 }, std::size_t { 8 }, std::size_t { 13 } })
		oscillatorControls[index].getSlider().setTooltip("Coarse oscillator tuning from two octaves down to two octaves up.");
	for (const auto index : { std::size_t { 4 }, std::size_t { 9 }, std::size_t { 14 } })
		oscillatorControls[index].getSlider().setTooltip("Fine oscillator tuning from -100 to +100 cents.");
	oscillatorControls[15].getSlider().setTooltip("Noise mixer level. Select white or pink noise in Performance / Noise.");
	const std::array filterNames { "Cutoff", "Resonance", "Key Track", "Env Amount", "Drive" };
	const std::array filterIds { parameters::filterCutoff, parameters::filterResonance, parameters::filterKeyTracking, parameters::filterEnvelopeAmount, parameters::filterDrive };
	for (std::size_t index = 0; index < filterControls.size(); ++index) addRotary(filterPanel, filterControls[index], filterNames[index], filterIds[index], filterAttachments[index]);
	filterControls[0].getSlider().setTooltip("Ladder cutoff frequency. Sweeps exponentially from dark to fully open.");
	filterControls[1].getSlider().setTooltip("Ladder emphasis. Adds a peak at cutoff and reaches self-oscillation near maximum.");
	filterControls[2].getSlider().setTooltip("Keyboard tracking. At 100%, cutoff rises one octave per keyboard octave.");
	filterControls[3].getSlider().setTooltip("Bipolar filter contour amount. Applies the filter envelope in octave pitch space.");
	filterControls[4].getSlider().setTooltip("Ladder input overload. Drives the nonlinear filter while compensating output level.");
	const std::array ampNames { "Attack", "Decay", "Sustain", "Release", "Velocity" };
	const std::array ampIds { parameters::ampAttack, parameters::ampDecay, parameters::ampSustain, parameters::ampRelease, parameters::ampVelocity };
	for (std::size_t index = 0; index < ampControls.size(); ++index) addRotary(ampPanel, ampControls[index], ampNames[index], ampIds[index], ampAttachments[index]);
	const std::array filterEnvelopeIds { parameters::filterAttack, parameters::filterDecay, parameters::filterSustain, parameters::filterRelease, parameters::filterVelocity };
	for (std::size_t index = 0; index < filterEnvelopeControls.size(); ++index) addRotary(filterEnvelopePanel, filterEnvelopeControls[index], ampNames[index], filterEnvelopeIds[index], filterEnvelopeAttachments[index]);
	const std::array voiceNames { "Detune", "Uni Spread", "Voice Pan", "Glide Time" };
	const std::array voiceIds { parameters::unisonDetune, parameters::unisonSpread, parameters::voiceWidth, parameters::glideTime };
	for (std::size_t index = 0; index < voiceControls.size(); ++index) addRotary(voicePanel, voiceControls[index], voiceNames[index], voiceIds[index], voiceAttachments[index]);
	for (auto* component : { static_cast<juce::Component*>(&outputFader), static_cast<juce::Component*>(&outputMeter) })
		ioPanel.addAndMakeVisible(*component);
	outputFader.setName("Master Output");
	outputFader.setSliderStyle(juce::Slider::LinearVertical);
	outputFader.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 76, 24);
	outputFader.setTextValueSuffix(" dB");
	outputFader.setDoubleClickReturnValue(true, 0.0);
	outputAttachment = std::make_unique<SliderAttachment>(pluginProcessor.getParameters(), parameters::masterOutput, outputFader);
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
	voiceControls[2].getSlider().setTooltip("Mixes polyphonic voices from centered at 0% to full round-robin stereo panning at 100%.");
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
	outputMeter.setStereoLevels(pluginProcessor.consumeOutputPeaks());
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
	ScalableEditor::resized(); auto& content = getContent(); title.setBounds(20, 16, 220, 40); presetNavigation.setBounds(260, 16, 320, 40); historyControls.setBounds(600, 16, 120, 40); status.setBounds(740, 16, 360, 40); presetBrowser.setBounds(content.getLocalBounds().reduced(20));
	oscillatorPanel.setBounds(20, 68, 1080, 184); filterPanel.setBounds(20, 268, 500, 180); voicePanel.setBounds(536, 268, 360, 180); ioPanel.setBounds(912, 268, 188, 180); ampPanel.setBounds(20, 464, 348, 216); filterEnvelopePanel.setBounds(384, 464, 348, 216); performancePanel.setBounds(748, 464, 352, 216);
	for (std::size_t index = 0; index < oscillatorControls.size(); ++index)
	{
		const auto x = 8 + static_cast<int>(index) * 67;
		oscillatorControls[index].setBounds(x, 40, 65, ui::RotaryControl::heightFor(ui::RotaryControl::Size::compact));
	}
	for (std::size_t index = 0; index < filterControls.size(); ++index) filterControls[index].setBounds(6 + static_cast<int>(index) * 98, 32, 94, 136);
	for (std::size_t index = 0; index < ampControls.size(); ++index) ampControls[index].setBounds(6 + static_cast<int>(index) * 67, 38, 65, 140);
	for (std::size_t index = 0; index < filterEnvelopeControls.size(); ++index) filterEnvelopeControls[index].setBounds(6 + static_cast<int>(index) * 67, 38, 65, 140);
	for (std::size_t index = 0; index < voiceControls.size(); ++index) voiceControls[index].setBounds(6 + static_cast<int>(index) * 87, 32, 83, 136);
	outputFader.setBounds(18, 34, 88, 132);
	outputMeter.setBounds(124, 38, 36, 104);
	voiceCountBox.setBounds(12, 58, 154, 28); performanceModeBox.setBounds(184, 58, 154, 28); qualityBox.setBounds(12, 116, 154, 28); unisonBox.setBounds(184, 116, 154, 28); glideBox.setBounds(12, 174, 154, 28); noiseBox.setBounds(184, 174, 154, 28);
	performanceLabels[0].setBounds(12, 38, 154, 18); performanceLabels[1].setBounds(184, 38, 154, 18); performanceLabels[2].setBounds(12, 96, 154, 18); performanceLabels[3].setBounds(184, 96, 154, 18); performanceLabels[4].setBounds(12, 154, 154, 18); performanceLabels[5].setBounds(184, 154, 154, 18); juce::ignoreUnused(content);
}
}