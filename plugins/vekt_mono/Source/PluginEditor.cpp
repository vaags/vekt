#include "PluginEditor.h"

#include "LfoDestinations.h"

#include <vector>

namespace vekt::mono
{
namespace
{
// Grid slot (two rows of four) of each single LFO destination, in depths() order: Filter, Amp, Drive, Noise,
// Detune, Spread, Filter Mode. Mode sits beside Filter.
constexpr std::array lfoSingleSlots { 0, 2, 3, 4, 5, 6, 1 };
}

double lfoPeakTravelPerSecond(const LfoDestination& destination, const juce::NormalisableRange<float>& knob,
	double base, float offset, LfoPolarity polarity, float rateHz) noexcept
{
	LfoReach reach;
	reach.add(offset, polarity);
	double share {};
	if (destination.scale == LfoTargetScale::linear)
		share = static_cast<double>((reach.highest - reach.lowest) * destination.targetPerOffset / (knob.end - knob.start));
	else
	{
		// Through the knob's skew, over the part of the swing its travel shows.
		const auto proportion = [&](float at)
		{
			return static_cast<double>(knob.convertTo0to1(juce::jlimit(knob.start, knob.end, static_cast<float>(lfoTargetValue(destination, base, at)))));
		};
		share = proportion(reach.highest) - proportion(reach.lowest);
	}
	return juce::MathConstants<double>::pi * static_cast<double>(rateHz) * std::abs(share);
}

LfoDot combineLfoDot(std::span<const LfoDotContribution> contributions) noexcept
{
	LfoDot dot;
	for (const auto& lfo : contributions) dot.opacity = std::max(dot.opacity, lfo.opacity);
	for (const auto& lfo : contributions)
	{
		// The share of this LFO the dot follows: as it gets too fast to follow, the band takes over the rest.
		const auto weight = std::clamp(lfo.opacity, 0.0f, 1.0f);
		// Output swings over 0..1 about 0.5 when unipolar, -1..1 about 0 when bipolar.
		const auto unipolar = lfo.polarity == LfoPolarity::unipolar;
		const auto centre = unipolar ? 0.5f : 0.0f;
		const auto halfSwing = unipolar ? 0.5f : 1.0f;
		dot.offset += lfo.offset * (weight * lfo.output + (1.0f - weight) * centre);
		dot.blurHalfWidth += (1.0f - weight) * std::abs(lfo.offset) * halfSwing;
	}
	return dot;
}

PluginEditor::PluginEditor(PluginProcessor& newProcessor)
	: ScalableEditor(newProcessor, editorWidth, ui::ScalableEditor::logicalHeight), pluginProcessor(newProcessor), historyControls(newProcessor.getUndoManager()),
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
		case 0: control.getSlider().setTooltip("Coarse oscillator tuning from two octaves down to two octaves up."); control.setBipolar(true); break;
		case 1: control.getSlider().setTooltip("Fine oscillator tuning from -100 to +100 cents."); control.setBipolar(true); break;
		case 2:
			control.setWaveformGuide(true);
			// Sine at 7:30, so the glyphs sit on the diagonals, in the corners of the square slider canvas (at
			// 12/3/6/9 o'clock they would fall outside it). Square to sine fills the old rotary gap round 6 o'clock;
			// the 4-to-0 wrap itself is at the sine, 7:30.
			control.setEndless(true, juce::MathConstants<float>::pi * 1.25f);
			control.getSlider().setTooltip("Waveform: sine, triangle, saw, square, then back to sine. Turns endlessly.");
			break;
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
	const std::array filterNames { "Cutoff", "Resonance", "Key Track", "Env Amt", "Drive", "Mode" };
	const std::array filterIds { parameters::filterCutoff, parameters::filterResonance, parameters::filterKeyTracking, parameters::filterEnvelopeAmount, parameters::filterDrive, parameters::filterMode };
	// A secondary arc links the Filter and Filter ADSR; their titles and labels
	// continue to identify the controls without relying on colour alone.
	const auto filterAccent = juce::Colour::fromRGB(123, 191, 173);
	for (std::size_t index = 0; index < filterControls.size(); ++index)
	{
		addRotary(filterPanel, filterControls[index], filterNames[index], filterIds[index], filterAttachments[index]);
		filterControls[index].getSlider().setColour(juce::Slider::rotarySliderFillColourId, filterAccent);
	}
	filterControls[0].getSlider().setTooltip("Cutoff frequency, the same for every filter type. Sweeps exponentially from dark to fully open.");
	filterControls[1].getSlider().setTooltip("Emphasis. Ladder: a resonant peak with natural bass loss that reaches self-oscillation near maximum in LP and Notch "
		"(toward HP it stops just short). "
		"SVF: strong resonance up to Q 20 that never self-oscillates and softens as Drive rises. K35: ringing up to Q 100 at 95 %, "
		"screaming self-oscillation in the last few percent, held and quenched by the played signal.");
	filterControls[2].getSlider().setTooltip("Keyboard tracking. At 100%, cutoff rises one octave per keyboard octave.");
	filterControls[3].getSlider().setTooltip("Unipolar filter contour amount. Applies the filter envelope in octave pitch space.");
	filterControls[4].getSlider().setTooltip("Input overload. Drives the nonlinear filter harder: the Ladder's stages saturate; the SVF's input "
		"saturates and its resonance softens; K35's output stage, already gritty at 0 dB, grows nastier and quenches the resonance.");
	filterControls[5].getSlider().setTooltip("Output mix from LP through Notch to HP. Ladder: a mix of its four stages over the same resonant ladder; "
		"SVF: its native LP and HP, with an exact Notch between them. K35: moves its input from the low-pass to the MS-20's "
		"6 dB/oct high-pass input; halfway is the full sound with a resonant peak at the cutoff.");
	// Filter type (ADR 0006, 0007): Ladder (4-pole, self-oscillating), SVF (2-pole, strongly resonant, never
	// self-oscillating) or K35 (2-pole, gritty; Mode reaches the MS-20 high-pass).
	const std::array filterTypeNames { "LADDER", "SVF", "K35" };
	const std::array filterTypeTooltips { "Ladder: 4-pole, 24 dB/oct nonlinear ladder. Thick, and self-oscillates near maximum Resonance in LP and Notch.",
		"SVF: 2-pole, 12 dB/oct state-variable filter. More open, strongly resonant, never self-oscillates; native LP, Notch and HP.",
		"K35: 2-pole, 12 dB/oct low-pass after the early MS-20 filter, with a diode-limited output stage. Gritty even at Drive 0, "
		"screaming at the top of Resonance. Mode turns it into the MS-20's 6 dB/oct high-pass." };
	// No undo manager of its own: each tab click opens one transaction.
	filterTypeAttachment = std::make_unique<juce::ParameterAttachment>(*pluginProcessor.getParameters().getParameter(parameters::filterType),
		[this](float value)
		{
			shownFilterType = juce::roundToInt(value);
			refreshFilterType();
		}, nullptr);
	for (std::size_t index = 0; index < filterTypeTabs.size(); ++index)
	{
		auto& tab = filterTypeTabs[index];
		tab.setButtonText(filterTypeNames[index]);
		tab.setName(juce::String("Filter Type ") + (index == 0 ? "Ladder" : index == 1 ? "SVF" : "K35"));
		tab.setTooltip(filterTypeTooltips[index]);
		tab.onClick = [this, index] { selectFilterType(static_cast<int>(index)); };
		filterPanel.addAndMakeVisible(tab);
	}
	filterTypeAttachment->sendInitialUpdate();
	filterPanel.addAndMakeVisible(qCompensationButton);
	// Short text so three filter tabs fit in the header; the name, tooltip and parameter keep the full title.
	qCompensationButton.setButtonText("Q Comp");
	qCompensationButton.setName("Q Compensation");
	qCompensationButton.setComponentID(parameters::filterQCompensation);
	qCompensationButton.setTooltip("Ladder only. Experimental input-path Q compensation. May change drive, harmonics and peaks; does not boost a "
		"free-running tone.");
	qCompensationAttachment = std::make_unique<ButtonAttachment>(pluginProcessor.getParameters(), parameters::filterQCompensation, qCompensationButton);
	const std::array ampNames { "Attack", "Decay", "Sustain", "Release", "Velocity" };
	const std::array ampIds { parameters::ampAttack, parameters::ampDecay, parameters::ampSustain, parameters::ampRelease, parameters::ampVelocity };
	for (std::size_t index = 0; index < ampControls.size(); ++index) addRotary(ampPanel, ampControls[index], ampNames[index], ampIds[index], ampAttachments[index]);
	const std::array filterEnvelopeIds { parameters::filterAttack, parameters::filterDecay, parameters::filterSustain, parameters::filterRelease, parameters::filterVelocity };
	for (std::size_t index = 0; index < filterEnvelopeControls.size(); ++index)
	{
		addRotary(filterEnvelopePanel, filterEnvelopeControls[index], ampNames[index], filterEnvelopeIds[index], filterEnvelopeAttachments[index]);
		filterEnvelopeControls[index].getSlider().setColour(juce::Slider::rotarySliderFillColourId, filterAccent);
	}
	const std::array voiceNames { "Detune", "Uni Spread", "Voice Pan", "Glide Time", "Drift" };
	const std::array voiceIds { parameters::unisonDetune, parameters::unisonSpread, parameters::voiceWidth, parameters::glideTime, parameters::drift };
	for (std::size_t index = 0; index < voiceControls.size(); ++index) addRotary(voicePanel, voiceControls[index], voiceNames[index], voiceIds[index], voiceAttachments[index]);
	for (auto* component : { static_cast<juce::Component*>(&outputScope), static_cast<juce::Component*>(&outputMeter),
		static_cast<juce::Component*>(&outputFader) })
		ioPanel.addAndMakeVisible(*component);
	outputFader.setName("Master Output");
	// Horizontal, beneath the scope and meter: the panel is too narrow for all three side by side.
	outputFader.setSliderStyle(juce::Slider::LinearHorizontal);
	outputFader.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 76, 20);
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
	addChoice(performancePanel, multicoreBox, { "Off", "On" }, parameters::multicore, multicoreAttachment);
	multicoreBox.setTooltip("Render voices on up to seven extra CPU cores (one fewer than your performance cores). Helps from a few voices up, most with high Quality or unison; the sound is identical either way. Leave off if your host already spreads tracks across cores.");
	const std::array performanceNames { "Voice count", "Mode", "Quality", "Unison", "Glide", "Multicore" };
	for (std::size_t index = 0; index < performanceLabels.size(); ++index)
	{
		performanceLabels[index].setText(performanceNames[index], juce::dontSendNotification);
		performanceLabels[index].setJustificationType(juce::Justification::centredLeft);
		performancePanel.addAndMakeVisible(performanceLabels[index]);
	}
	activeVoicesLabel.setName("Active voices");
	activeVoicesLabel.setJustificationType(juce::Justification::centredRight);
	activeVoicesLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(170, 178, 176));
	activeVoicesLabel.setTooltip("Voices sounding now, including release tails, out of the Voice count limit.");
	performancePanel.addAndMakeVisible(activeVoicesLabel);
	noiseBox.setTooltip("White or pink noise source.");
	qualityBox.setTooltip("1x is the zero-oversampling default; 2x uses minimum-phase IIR, and 4x/8x use linear-phase FIR. Higher factors reduce filter aliasing on bright high notes but use more CPU and add latency. Changing it cuts any sounding notes.");
	performanceModeBox.setTooltip("Mono retriggers each note; Mono Legato keeps the envelope active while notes overlap.");
	heldKeyReturnButton.setTooltip("When enabled, releasing the active mono note returns to the selected still-held key (last or lowest priority).");
	glideBox.setTooltip("Always glides every note change; Legato glides only while another note is held.");
	voiceControls[4].getSlider().setTooltip("Analog instability. Each oscillator wanders in pitch independently, and each voice gets "
		"its own cutoff, envelope-time and level offsets, like separate analog voice cards. Subtle up to about 30 %; "
		"at 100 % pitch wanders up to +/-28 ct (plus a fixed +/-12 ct per voice), three times faster.");
	voiceControls[2].getSlider().setTooltip("Mixes polyphonic voices from centered at 0% to full round-robin stereo panning at 100%.");
	getContent().addAndMakeVisible(lfoPanel);
	const std::array destinationNames { "Osc 1 Pitch", "Osc 2 Pitch", "Osc 3 Pitch", "Osc 1 Morph", "Osc 2 Morph", "Osc 3 Morph",
		"Osc 1 Width", "Osc 2 Width", "Osc 3 Width", "Osc 1 Level", "Osc 2 Level", "Osc 3 Level",
		"Filter", "Amp", "Drive", "Noise", "Detune", "Spread", "Filter Mode" };
	for (std::size_t index = 0; index < lfoControls.size(); ++index)
	{
		const auto& ids = parameters::lfos[index];
		auto& controls = lfoControls[index];
		const auto prefix = "LFO " + juce::String(static_cast<int>(index) + 1) + " ";
		addChoice(lfoPanel, controls.shape, { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Smooth Random" }, ids.shape, controls.shapeAttachment);
		addChoice(lfoPanel, controls.polarity, { "Bipolar", "Unipolar" }, ids.polarity, controls.polarityAttachment);
		addChoice(lfoPanel, controls.mode, { "Free", "Retrigger", "One Shot" }, ids.mode, controls.modeAttachment);
		controls.shape.setName(prefix + "Shape");
		controls.polarity.setName(prefix + "Polarity");
		controls.mode.setName(prefix + "Mode");
		controls.polarity.setTooltip("Bipolar swings -1 to +1 (vibrato); Unipolar swings 0 to 1 (only raises or only lowers a destination).");
		controls.mode.setTooltip("Free runs continuously; Retrigger restarts with each note; One Shot runs one cycle per note and holds, like an envelope.");
		lfoPanel.addAndMakeVisible(controls.sync);
		controls.sync.setName(prefix + "Sync");
		controls.sync.setTooltip("Sets the rate in note divisions of the host tempo.");
		controls.syncAttachment = std::make_unique<ButtonAttachment>(pluginProcessor.getParameters(), ids.sync, controls.sync);
		controls.sync.onClick = [this] { refreshLfoVisibility(); };
		const std::array knobs { &controls.rate, &controls.division, &controls.amount, &controls.phase, &controls.delay, &controls.fade };
		const std::array knobNames { "Rate", "Division", "Amount", "Phase", "Delay", "Fade" };
		const std::array knobIds { ids.rate, ids.division, ids.amount, ids.phase, ids.delay, ids.fade };
		for (std::size_t knob = 0; knob < knobs.size(); ++knob)
		{
			addRotary(lfoPanel, *knobs[knob], knobNames[knob], knobIds[knob], controls.knobAttachments[knob]);
			knobs[knob]->setLayout(ui::RotaryControl::Size::compact, 62);
			knobs[knob]->setName(prefix + knobNames[knob]);
			knobs[knob]->getSlider().setName(prefix + knobNames[knob]);
		}
		controls.phase.setEndless(true);
		controls.amount.getSlider().setTooltip("Master depth: scales every destination of this LFO.");
		controls.delay.getSlider().setTooltip("Silent time after each note starts.");
		controls.fade.getSlider().setTooltip("Fade-in time after the delay.");
		const auto depthIds = ids.depths();
		for (std::size_t depth = 0; depth < controls.depths.size(); ++depth)
		{
			auto& slider = controls.depths[depth];
			slider.setSliderStyle(juce::Slider::LinearHorizontal);
			slider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
			slider.getProperties().set("bipolar", true);
			slider.setPopupDisplayEnabled(true, false, &getContent());
			slider.setName(prefix + destinationNames[depth]);
			slider.setTooltip(prefix + destinationNames[depth] + " depth. Double-click to reset.");
			lfoPanel.addAndMakeVisible(slider);
			controls.depthAttachments[depth] = std::make_unique<SliderAttachment>(pluginProcessor.getParameters(), depthIds[depth], slider);
			slider.setDoubleClickReturnValue(true, 0.0);
		}
		auto& tab = lfoTabs[index];
		tab.setButtonText(juce::String(static_cast<int>(index) + 1));
		tab.setName(prefix + "Tab");
		tab.setRadioGroupId(0x4c464f);
		tab.onClick = [this, index] { selectLfo(index); };
		lfoPanel.addAndMakeVisible(tab);
	}
	const std::array destinationLabels { "Pitch", "Morph", "Width", "Level", "Osc 1", "Osc 2", "Osc 3", "Filter", "Amp", "Drive", "Noise", "Detune", "Spread", "Mode" };
	for (std::size_t index = 0; index < lfoDestinationLabels.size(); ++index)
	{
		auto& label = lfoDestinationLabels[index];
		label.setText(destinationLabels[index], juce::dontSendNotification);
		label.setFont(juce::FontOptions(13.0f));
		label.setColour(juce::Label::textColourId, juce::Colour::fromRGB(170, 178, 176));
		label.setJustificationType(index >= 4 && index < 7 ? juce::Justification::centredLeft : juce::Justification::centred);
		lfoPanel.addAndMakeVisible(label);
	}
	getContent().addAndMakeVisible(vibratoPanel);
	addRotary(vibratoPanel, vibratoRateControl, "Rate", parameters::vibratoRate, vibratoRateAttachment);
	addRotary(vibratoPanel, vibratoDepthControl, "Depth", parameters::vibratoDepth, vibratoDepthAttachment);
	for (auto* control : { &vibratoRateControl, &vibratoDepthControl })
	{
		const auto qualifiedName = "Vibrato " + control->getName();
		control->setName(qualifiedName);
		control->getSlider().setName(qualifiedName);
	}
	vibratoDepthControl.getSlider().setTooltip("Vibrato depth reached with the mod wheel or aftertouch fully up.");
	addChoice(vibratoPanel, vibratoShapeBox, { "Sine", "Triangle" }, parameters::vibratoShape, vibratoShapeAttachment);
	vibratoShapeBox.setName("Vibrato Shape");
	vibratoShapeLabel.setText("Shape", juce::dontSendNotification);
	vibratoMeterLabel.setText("Wheel / AT", juce::dontSendNotification);
	for (auto* label : { &vibratoShapeLabel, &vibratoMeterLabel })
	{
		label->setJustificationType(juce::Justification::centredLeft);
		vibratoPanel.addAndMakeVisible(*label);
	}
	vibratoMeter.setName("Vibrato Control");
	vibratoMeter.setTooltip("On-screen mod wheel: drag it, or use the arrow keys (Shift for fine). The vibrato follows it, "
		"the mod wheel or aftertouch, whichever is higher. Double-click to reset.");
	vibratoMeter.setPopupDisplayEnabled(true, false, &getContent());
	vibratoPanel.addAndMakeVisible(vibratoMeter);
	vibratoAmountAttachment = std::make_unique<SliderAttachment>(pluginProcessor.getParameters(), parameters::vibratoAmount, vibratoMeter);
	vibratoMeter.setDoubleClickReturnValue(true, 0.0);
	selectLfo(0);
	refreshPresetLabel();
	resized();
	// Each LFO destination's knob, found by the parameter it is attached to.
	std::vector<ui::RotaryControl*> rotaries { &noiseLevelControl };
	for (auto& control : oscillatorControls) rotaries.push_back(&control);
	for (auto& control : filterControls) rotaries.push_back(&control);
	for (auto& control : voiceControls) rotaries.push_back(&control);
	for (std::size_t destination = 0; destination < lfoTargets.size(); ++destination)
		if (const auto* target = lfoDestinations[destination].target)
			for (auto* control : rotaries)
				if (control->getComponentID() == target) lfoTargets[destination] = control;
	refreshModulationRings(juce::Time::getMillisecondCounterHiRes() * 0.001);
	timerCallback();
	// Fast enough for the LFO activity lights to move smoothly.
	startTimerHz(30);
}

PluginEditor::~PluginEditor() { setLookAndFeel(nullptr); }
void PluginEditor::refreshFilterType()
{
	const auto type = shownFilterType;
	for (std::size_t index = 0; index < filterTypeTabs.size(); ++index)
		filterTypeTabs[index].setToggleState(static_cast<int>(index) == type, juce::dontSendNotification);
	// Disabled, not hidden, so the panel keeps its layout (ADR 0006, 0007): Q Comp is Ladder-only.
	qCompensationButton.setEnabled(type == 0);
}

// A tab click as one undoable step and a complete host gesture.
void PluginEditor::selectFilterType(int type)
{
	pluginProcessor.getUndoManager().beginNewTransaction("Filter Type");
	filterTypeAttachment->setValueAsCompleteGesture(static_cast<float>(type));
}
void PluginEditor::selectLfo(std::size_t index)
{
	selectedLfo = index;
	for (std::size_t tab = 0; tab < lfoTabs.size(); ++tab) lfoTabs[tab].setToggleState(tab == index, juce::dontSendNotification);
	refreshLfoVisibility();
}
void PluginEditor::refreshLfoVisibility()
{
	for (std::size_t index = 0; index < lfoControls.size(); ++index)
	{
		auto& controls = lfoControls[index];
		const auto shown = index == selectedLfo;
		for (auto* component : { static_cast<juce::Component*>(&controls.shape), static_cast<juce::Component*>(&controls.polarity),
				static_cast<juce::Component*>(&controls.mode), static_cast<juce::Component*>(&controls.sync),
				static_cast<juce::Component*>(&controls.amount), static_cast<juce::Component*>(&controls.phase),
				static_cast<juce::Component*>(&controls.delay), static_cast<juce::Component*>(&controls.fade) })
			component->setVisible(shown);
		for (auto& depth : controls.depths) depth.setVisible(shown);
		// One knob position: Hz when free, a note division when synced.
		const auto synced = controls.sync.getToggleState();
		controls.rate.setVisible(shown && !synced);
		controls.division.setVisible(shown && synced);
	}
}
void PluginEditor::refreshModulationRings(double nowSeconds)
{
	auto& state = pluginProcessor.getParameters();
	const auto value = [&state](const char* identifier) { return state.getRawParameterValue(identifier)->load(); };
	// The flag is current; the frame is slightly in the past. Both must agree that a voice sounds.
	const auto frame = lfoTimeline.advance(pluginProcessor.getLfoHistory(), nowSeconds);
	const auto sounding = pluginProcessor.isLfoDisplayActive() && frame && frame->tag != 0;
	// Before the host prepares the processor, assume a typical rate for the sample-rate-dependent cutoff limit.
	const auto sampleRate = pluginProcessor.getSampleRate() > 0.0 ? pluginProcessor.getSampleRate() : 48'000.0;
	std::array<LfoReach, 19> reaches {};
	std::array<std::array<LfoDotContribution, parameters::lfos.size()>, 19> contributions {};
	std::array<std::size_t, 19> contributionCounts {};
	for (std::size_t lfo = 0; lfo < parameters::lfos.size(); ++lfo)
	{
		const auto& ids = parameters::lfos[lfo];
		const auto amount = value(ids.amount) * 0.01f;
		const auto polarity = static_cast<LfoPolarity>(juce::roundToInt(value(ids.polarity)));
		const auto output = sounding ? frame->values[lfo] : 0.0f;
		const auto rate = pluginProcessor.getLfoDisplayRate(lfo);
		const auto depthIds = ids.depths();
		for (std::size_t destination = 0; destination < reaches.size(); ++destination)
		{
			const auto& target = lfoDestinations[destination];
			const auto offset = amount * value(depthIds[destination]) * target.offsetPerDepth;
			if (juce::exactlyEqual(offset, 0.0f)) continue;
			reaches[destination].add(offset, polarity);
			// Visible while this LFO moves the dot slowly enough on this knob to follow.
			auto opacity = 1.0f;
			if (const auto* control = lfoTargets[destination])
				opacity = control->getModulationRing().dotOpacityForSpeed(lfoPeakTravelPerSecond(target,
					state.getParameter(target.target)->getNormalisableRange(), value(target.target), offset, polarity, rate));
			contributions[destination][contributionCounts[destination]++] = { offset, output, polarity, opacity };
		}
	}
	for (std::size_t destination = 0; destination < reaches.size(); ++destination)
	{
		auto* control = lfoTargets[destination];
		if (control == nullptr) continue;
		const auto& reach = reaches[destination];
		if (reach.isEmpty())
		{
			control->setModulation({});
			continue;
		}
		const auto& target = lfoDestinations[destination];
		const auto base = static_cast<double>(value(target.target));
		// What the voice can reach, which for pitch and cutoff extends past the knob; the ring marks that overflow.
		const auto& knobRange = state.getParameter(target.target)->getNormalisableRange();
		const auto bounds = lfoTargetBounds(target, knobRange.start, knobRange.end, sampleRate);
		ui::ModulationDisplay display;
		display.lowest = bounds.clamp(lfoTargetValue(target, base, reach.lowest));
		display.highest = bounds.clamp(lfoTargetValue(target, base, reach.highest));
		const auto dot = combineLfoDot(std::span(contributions[destination].data(), contributionCounts[destination]));
		// Per-voice drift can carry the output slightly past full scale; keep the dot and band on the arc.
		const auto onArc = [&](float offset)
		{
			return bounds.clamp(lfoTargetValue(target, base, std::clamp(offset, reach.lowest, reach.highest)));
		};
		if (sounding && dot.opacity > 0.0f)
		{
			display.current = onArc(dot.offset);
			display.currentOpacity = dot.opacity;
		}
		if (sounding && dot.blurHalfWidth > 0.0f)
			display.blur = ui::ModulationDisplay::Band { onArc(dot.offset - dot.blurHalfWidth), onArc(dot.offset + dot.blurHalfWidth) };
		control->setModulation(display);
	}
}
void PluginEditor::addRotary(ui::Panel& panel, ui::RotaryControl& control, const char* name, const char* identifier, std::unique_ptr<SliderAttachment>& attachment)
{
	control.setLabel(name); control.setComponentID(identifier); panel.addAndMakeVisible(control); attachment = std::make_unique<SliderAttachment>(pluginProcessor.getParameters(), identifier, control.getSlider());
	// The attachment installs the parameter's text formatting; refresh the readout, which was set before it.
	control.refreshValueText();
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
	for (std::size_t index = 0; index < lfoTabs.size(); ++index) lfoTabs[index].setLevel(pluginProcessor.getLfoDisplayValue(index));
	vibratoMeter.setLevel(pluginProcessor.getVibratoControlDisplay());
	activeVoicesLabel.setText(juce::String(pluginProcessor.getSoundingVoiceDisplay()) + " active", juce::dontSendNotification);
	refreshLfoVisibility();
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
	ScalableEditor::resized(); auto& content = getContent(); title.setBounds(20, 16, 220, 40); presetNavigation.setBounds(260, 16, 320, 40); historyControls.setBounds(600, 16, 120, 40); status.setBounds(740, 16, editorWidth - 760, 40);
	presetBrowser.setBounds(content.getLocalBounds().reduced(20));
	// Columns match the ADSR/Performance row below: 348 px panels with 16 px gaps.
	for (std::size_t index = 0; index < oscillatorPanels.size(); ++index) oscillatorPanels[index].setBounds(20 + static_cast<int>(index) * 364, 68, 348, 184);
	filterPanel.setBounds(20, 268, 348, 180); voicePanel.setBounds(384, 268, 347, 180); noisePanel.setBounds(747, 268, 190, 180); ioPanel.setBounds(953, 268, 147, 180); ampPanel.setBounds(20, 464, 348, 216); filterEnvelopePanel.setBounds(384, 464, 348, 216); performancePanel.setBounds(748, 464, 352, 216);
	for (std::size_t index = 0; index < oscillatorControls.size(); ++index)
	{
		const auto x = 6 + static_cast<int>(index % 5) * 67;
		oscillatorControls[index].setBounds(x, 40, 65, ui::RotaryControl::heightFor(ui::RotaryControl::Size::compact));
	}
	for (std::size_t index = 0; index < filterControls.size(); ++index) filterControls[index].setBounds(6 + static_cast<int>(index) * 56, 32, 55, 136);
	filterTypeTabs[0].setBounds(64, 5, 66, 24);
	filterTypeTabs[1].setBounds(134, 5, 44, 24);
	filterTypeTabs[2].setBounds(182, 5, 44, 24);
	qCompensationButton.setBounds(232, 5, 110, 24);
	for (std::size_t index = 0; index < ampControls.size(); ++index) ampControls[index].setBounds(6 + static_cast<int>(index) * 67, 38, 65, 140);
	for (std::size_t index = 0; index < filterEnvelopeControls.size(); ++index) filterEnvelopeControls[index].setBounds(6 + static_cast<int>(index) * 67, 38, 65, 140);
	for (std::size_t index = 0; index < voiceControls.size(); ++index) voiceControls[index].setBounds(6 + static_cast<int>(index) * 67, 32, 65, 136);
	noiseTypeLabel.setBounds(12, 38, 96, 18); noiseBox.setBounds(12, 58, 96, 28); noiseLevelControl.setBounds(116, 32, 65, 136);
	outputScope.setBounds(12, 36, 123, 66);
	outputMeter.setBounds(12, 106, 123, 18);
	outputFader.setBounds(12, 128, 123, 44);
	voiceCountBox.setBounds(12, 58, 154, 28); performanceModeBox.setBounds(184, 58, 154, 28); qualityBox.setBounds(12, 116, 154, 28); unisonBox.setBounds(184, 116, 154, 28); glideBox.setBounds(12, 174, 154, 28); multicoreBox.setBounds(184, 174, 154, 28);
	lfoPanel.setBounds(1116, 68, 348, 380);
	vibratoPanel.setBounds(1116, 464, 348, 216);
	vibratoRateControl.setBounds(6, 38, 65, 140);
	vibratoDepthControl.setBounds(73, 38, 65, 140);
	vibratoShapeLabel.setBounds(156, 38, 180, 18); vibratoShapeBox.setBounds(156, 58, 180, 28);
	vibratoMeterLabel.setBounds(156, 112, 180, 18);
	// The slider insets its travel by its thumb radius (half its height), so the bar itself spans 156-336 like the label.
	vibratoMeter.setBounds(145, 130, 202, 22);
	for (std::size_t index = 0; index < lfoTabs.size(); ++index) lfoTabs[index].setBounds(64 + static_cast<int>(index) * 60, 5, 54, 24);
	for (auto& controls : lfoControls)
	{
		controls.sync.setBounds(262, 5, 80, 24);
		controls.shape.setBounds(12, 40, 124, 26); controls.polarity.setBounds(142, 40, 94, 26); controls.mode.setBounds(242, 40, 94, 26);
		const std::array knobs { &controls.amount, &controls.phase, &controls.delay, &controls.fade };
		for (auto* rate : { &controls.rate, &controls.division }) rate->setBounds(6, 72, 65, ui::RotaryControl::heightFor(ui::RotaryControl::Size::compact));
		for (std::size_t knob = 0; knob < knobs.size(); ++knob)
			knobs[knob]->setBounds(73 + static_cast<int>(knob) * 67, 72, 65, ui::RotaryControl::heightFor(ui::RotaryControl::Size::compact));
		// Oscillator grid: rows Osc 1-3, columns Pitch/Morph/Width/Level; then the single destinations in two rows of four.
		for (std::size_t column = 0; column < 4; ++column)
			for (std::size_t row = 0; row < 3; ++row)
				controls.depths[column * 3 + row].setBounds(56 + static_cast<int>(column) * 72, 214 + static_cast<int>(row) * 24, 68, 22);
		for (std::size_t single = 0; single < lfoSingleSlots.size(); ++single)
			controls.depths[12 + single].setBounds(12 + lfoSingleSlots[single] % 4 * 84, 306 + lfoSingleSlots[single] / 4 * 42, 78, 22);
	}
	for (std::size_t column = 0; column < 4; ++column) lfoDestinationLabels[column].setBounds(56 + static_cast<int>(column) * 72, 198, 68, 14);
	for (std::size_t row = 0; row < 3; ++row) lfoDestinationLabels[4 + row].setBounds(12, 214 + static_cast<int>(row) * 24, 44, 22);
	for (std::size_t single = 0; single < lfoSingleSlots.size(); ++single)
		lfoDestinationLabels[7 + single].setBounds(12 + lfoSingleSlots[single] % 4 * 84, 290 + lfoSingleSlots[single] / 4 * 42, 78, 14);
	performanceLabels[0].setBounds(12, 38, 90, 18); activeVoicesLabel.setBounds(102, 38, 64, 18); performanceLabels[1].setBounds(184, 38, 50, 18); performanceLabels[2].setBounds(12, 96, 154, 18); performanceLabels[3].setBounds(184, 96, 154, 18); performanceLabels[4].setBounds(12, 154, 154, 18); performanceLabels[5].setBounds(184, 154, 154, 18); heldKeyReturnButton.setBounds(238, 34, 100, 22); priorityBox.setBounds(127, 5, 145, 26); juce::ignoreUnused(content);
}
}
