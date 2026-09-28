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
	for (auto* panel : { &oscillatorPanels[0], &oscillatorPanels[1], &oscillatorPanels[2], &noisePanel, &filterPanel, &voicePanel, &ioPanel, &ampPanel, &filterEnvelopePanel, &performancePanel }) getContent().addAndMakeVisible(*panel);
	getContent().addAndMakeVisible(title); getContent().addAndMakeVisible(status);
	getContent().addAndMakeVisible(historyControls);
	getContent().addAndMakeVisible(presetNavigation);
	getContent().addChildComponent(presetBrowser);
	// Each oscillator panel reads pitch, shape, then mixer level.
	const std::array oscillatorNames { "Octave", "Fine", "Morph", "Width", "Level" };
	const std::array oscillatorIds { parameters::osc1Octave, parameters::osc1Fine, parameters::osc1Morph, parameters::osc1PulseWidth, parameters::osc1Level,
		parameters::osc2Octave, parameters::osc2Fine, parameters::osc2Morph, parameters::osc2PulseWidth, parameters::osc2Level,
		parameters::osc3Octave, parameters::osc3Fine, parameters::osc3Morph, parameters::osc3PulseWidth, parameters::osc3Level };
	for (std::size_t index = 0; index < oscillatorControls.size(); ++index)
	{
		const auto oscillator = index / oscillatorNames.size();
		auto& control = oscillatorControls[index];
		addRotary(oscillatorPanels[oscillator], control, oscillatorNames[index % oscillatorNames.size()], oscillatorIds[index], oscillatorAttachments[index]);
		control.setLayout(ui::RotaryControl::Size::compact, 62);
		// Keep panel-qualified names so accessibility and tests can tell the oscillators apart.
		const auto qualifiedName = oscillatorPanels[oscillator].getName() + " " + oscillatorNames[index % oscillatorNames.size()];
		control.setName(qualifiedName);
		control.getSlider().setName(qualifiedName);
		switch (index % oscillatorNames.size())
		{
		case 0: control.getSlider().setTooltip("Coarse oscillator tuning from two octaves down to two octaves up."); break;
		case 1: control.getSlider().setTooltip("Fine oscillator tuning from -100 to +100 cents."); break;
		case 2: control.setWaveformGuide(true); break;
		default: break;
		}
	}
	addRotary(noisePanel, noiseLevelControl, "Level", parameters::noiseLevel, noiseLevelAttachment);
	noiseLevelControl.setName("Noise Level");
	noiseLevelControl.getSlider().setName("Noise Level");
	noiseLevelControl.getSlider().setTooltip("Noise mixer level.");
	noiseTypeLabel.setText("Type", juce::dontSendNotification);
	noiseTypeLabel.setJustificationType(juce::Justification::centredLeft);
	noisePanel.addAndMakeVisible(noiseTypeLabel);
	const std::array filterNames { "Cutoff", "Resonance", "Key Track", "Env Amount", "Drive" };
	const std::array filterIds { parameters::filterCutoff, parameters::filterResonance, parameters::filterKeyTracking, parameters::filterEnvelopeAmount, parameters::filterDrive };
	for (std::size_t index = 0; index < filterControls.size(); ++index) addRotary(filterPanel, filterControls[index], filterNames[index], filterIds[index], filterAttachments[index]);
	filterControls[0].getSlider().setTooltip("Ladder cutoff frequency. Sweeps exponentially from dark to fully open.");
	filterControls[1].getSlider().setTooltip("Ladder emphasis. Adds a resonant peak with natural bass loss and reaches self-oscillation near maximum.");
	filterControls[2].getSlider().setTooltip("Keyboard tracking. At 100%, cutoff rises one octave per keyboard octave.");
	filterControls[3].getSlider().setTooltip("Unipolar filter contour amount. Applies the filter envelope in octave pitch space.");
	filterControls[4].getSlider().setTooltip("Ladder input overload. Drives the nonlinear filter while compensating output level.");
	filterPanel.addAndMakeVisible(qCompensationButton);
	qCompensationButton.setName("Q Compensation");
	qCompensationButton.setComponentID(parameters::filterQCompensation);
	qCompensationButton.setTooltip("Experimental input-path Q compensation. May change drive, harmonics and peaks; does not boost a free-running tone.");
	qCompensationAttachment = std::make_unique<ButtonAttachment>(pluginProcessor.getParameters(), parameters::filterQCompensation, qCompensationButton);
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
	addChoice(noisePanel, noiseBox, { "Off", "White", "Pink" }, parameters::noiseType, noiseAttachment);
	addChoice(performancePanel, voiceCountBox, { "2", "4", "8", "12", "16" }, parameters::voiceCount, voiceCountAttachment);
	addChoice(performancePanel, performanceModeBox, { "Poly", "Mono", "Mono Legato" }, parameters::performanceMode, performanceModeAttachment);
	addChoice(voicePanel, priorityBox, { "Last priority", "Low priority" }, parameters::notePriority, priorityAttachment);
	priorityBox.setTooltip("Mono note priority; low priority keeps the lowest held key sounding.");
	performancePanel.addAndMakeVisible(heldKeyReturnButton);
	heldKeyReturnAttachment = std::make_unique<ButtonAttachment>(pluginProcessor.getParameters(), parameters::heldKeyReturn, heldKeyReturnButton);
	addChoice(performancePanel, qualityBox, { "1x", "2x", "4x", "8x" }, parameters::quality, qualityAttachment);
	addChoice(performancePanel, unisonBox, { "1x", "2x", "4x" }, parameters::unison, unisonAttachment);
	addChoice(performancePanel, glideBox, { "Off", "Always", "Legato" }, parameters::glideMode, glideAttachment);
	const std::array performanceNames { "Voice count", "Mode", "Quality", "Unison", "Glide" };
	for (std::size_t index = 0; index < performanceLabels.size(); ++index)
	{
		performanceLabels[index].setText(performanceNames[index], juce::dontSendNotification);
		performanceLabels[index].setJustificationType(juce::Justification::centredLeft);
		performancePanel.addAndMakeVisible(performanceLabels[index]);
	}
	noiseBox.setTooltip("White or pink noise source.");
	qualityBox.setTooltip("1x is the zero-oversampling default; 2x uses minimum-phase IIR, and 4x/8x use linear-phase FIR. Higher factors use more CPU and add latency. Changing it cuts any sounding notes.");
	performanceModeBox.setTooltip("Mono retriggers each note; Mono Legato keeps the envelope active while notes overlap.");
	heldKeyReturnButton.setTooltip("When enabled, releasing the active mono note returns to the selected still-held key (last or lowest priority).");
	glideBox.setTooltip("Always glides every note change; Legato glides only while another note is held.");
	voiceControls[2].getSlider().setTooltip("Mixes polyphonic voices from centered at 0% to full round-robin stereo panning at 100%.");
	refreshPresetLabel();
	resized();
	timerCallback();
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
	const auto activeQuality = pluginProcessor.getActiveQuality();
	const auto qualityName = activeQuality == 0 ? "1x" : activeQuality == 1 ? "2x IIR"
		: activeQuality == 2 ? "4x FIR" : "8x FIR";
	juce::String message = "Quality: " + juce::String(qualityName)
		+ " • " + juce::String(pluginProcessor.getLatencySamples()) + " smp";
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
	// Columns match the ADSR/Performance row below: 348 px panels with 16 px gaps.
	for (std::size_t index = 0; index < oscillatorPanels.size(); ++index) oscillatorPanels[index].setBounds(20 + static_cast<int>(index) * 364, 68, 348, 184);
	filterPanel.setBounds(20, 268, 348, 180); voicePanel.setBounds(384, 268, 280, 180); noisePanel.setBounds(680, 268, 216, 180); ioPanel.setBounds(912, 268, 188, 180); ampPanel.setBounds(20, 464, 348, 216); filterEnvelopePanel.setBounds(384, 464, 348, 216); performancePanel.setBounds(748, 464, 352, 216);
	for (std::size_t index = 0; index < oscillatorControls.size(); ++index)
	{
		const auto x = 6 + static_cast<int>(index % 5) * 67;
		oscillatorControls[index].setBounds(x, 40, 65, ui::RotaryControl::heightFor(ui::RotaryControl::Size::compact));
	}
	for (std::size_t index = 0; index < filterControls.size(); ++index) filterControls[index].setBounds(6 + static_cast<int>(index) * 67, 32, 65, 136);
	qCompensationButton.setBounds(174, 5, 166, 24);
	for (std::size_t index = 0; index < ampControls.size(); ++index) ampControls[index].setBounds(6 + static_cast<int>(index) * 67, 38, 65, 140);
	for (std::size_t index = 0; index < filterEnvelopeControls.size(); ++index) filterEnvelopeControls[index].setBounds(6 + static_cast<int>(index) * 67, 38, 65, 140);
	for (std::size_t index = 0; index < voiceControls.size(); ++index) voiceControls[index].setBounds(6 + static_cast<int>(index) * 67, 32, 65, 136);
	noiseTypeLabel.setBounds(12, 38, 112, 18); noiseBox.setBounds(12, 58, 112, 28); noiseLevelControl.setBounds(140, 32, 65, 136);
	outputFader.setBounds(18, 34, 88, 132);
	outputMeter.setBounds(124, 38, 36, 104);
	voiceCountBox.setBounds(12, 58, 154, 28); performanceModeBox.setBounds(184, 58, 154, 28); qualityBox.setBounds(12, 116, 154, 28); unisonBox.setBounds(184, 116, 154, 28); glideBox.setBounds(12, 174, 154, 28);
	performanceLabels[0].setBounds(12, 38, 154, 18); performanceLabels[1].setBounds(184, 38, 50, 18); performanceLabels[2].setBounds(12, 96, 154, 18); performanceLabels[3].setBounds(184, 96, 154, 18); performanceLabels[4].setBounds(12, 154, 154, 18); heldKeyReturnButton.setBounds(238, 34, 100, 22); priorityBox.setBounds(127, 5, 145, 26); juce::ignoreUnused(content);
}
}
