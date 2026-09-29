#include <PluginEditor.h>
#include <Parameters.h>
#include "../../plugins/vekt_glimmer/Source/PluginEditor.h"
#include "../../plugins/vekt_mono/Source/PluginEditor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>

namespace
{
juce::Button* findNamedButton(juce::Component& parent, const juce::String& name)
{
	for (auto* child : parent.getChildren())
	{
		if (auto* button = dynamic_cast<juce::Button*>(child); button != nullptr && button->getName() == name)
			return button;
		if (auto* button = findNamedButton(*child, name)) return button;
	}
	return nullptr;
}

void checkVisibleBounds(juce::Component& parent)
{
	for (auto* child : parent.getChildren())
	{
		if (!child->isVisible())
			continue;
		INFO("Component: " << child->getName().toStdString());
		REQUIRE_FALSE(child->getBounds().isEmpty());
		REQUIRE(parent.getLocalBounds().contains(child->getBounds()));
		// A ListBox's viewport intentionally clips/recycles rows beyond its bounds.
		if (dynamic_cast<juce::ListBox*>(child) != nullptr) continue;
		checkVisibleBounds(*child);
	}
}

vekt::ui::LevelMeter* findMeter(juce::Component& parent, const juce::String& name)
{
	for (auto* child : parent.getChildren())
	{
		if (auto* meter = dynamic_cast<vekt::ui::LevelMeter*>(child); meter != nullptr && meter->getName() == name)
			return meter;
		if (auto* meter = findMeter(*child, name))
			return meter;
	}
	return nullptr;
}

// Opt-in render artefact for visual review. FileOutputStream appends to an existing file, so
// truncate it: an appended PNG still opens as the stale first image.
void writeSnapshot(juce::Component& editor, const char* path)
{
	const auto image = editor.createComponentSnapshot(editor.getLocalBounds(), true, 2.0f);
	juce::FileOutputStream stream { juce::File(juce::String(path)) };
	REQUIRE(stream.openedOk());
	REQUIRE(stream.setPosition(0));
	REQUIRE(stream.truncate().wasOk());
	REQUIRE(juce::PNGImageFormat().writeImageToStream(image, stream));
}

vekt::ui::RotaryControl* findRotary(juce::Component& parent, const juce::String& name)
{
	for (auto* child : parent.getChildren())
	{
		if (auto* rotary = dynamic_cast<vekt::ui::RotaryControl*>(child); rotary != nullptr && rotary->getName() == name)
			return rotary;
		if (auto* rotary = findRotary(*child, name))
			return rotary;
	}
	return nullptr;
}

void checkMeterBounds(juce::Component& content, int minimumWidth)
{
	for (const auto* name : { "IN", "OUT" })
	{
		auto* meter = findMeter(content, name);
		REQUIRE(meter != nullptr);
		REQUIRE(meter->getWidth() >= minimumWidth);
		REQUIRE_FALSE(meter->getBounds().isEmpty());
	}
}
}

TEST_CASE("Rav editor keeps its controls within the 16:10 canvas", "[processor][ui]")
{
	juce::ScopedJuceInitialiser_GUI initialiseJuce;
	vekt::rav::PluginProcessor processor;
	vekt::rav::PluginEditor editor(processor);
	REQUIRE(editor.getWidth() == 1120);
	REQUIRE(editor.getHeight() == 700);
	REQUIRE(editor.getConstrainer()->getFixedAspectRatio() == 1.6);

	for (const auto width : { 1120, 1680, 2240 })
	{
		editor.setSize(width, width * 10 / 16);
		checkVisibleBounds(editor.getContent());
		checkMeterBounds(editor.getContent(), 32);
	}
	const auto find = [&](const juce::String& name) -> juce::Component&
	{
		for (auto* child : editor.getContent().getChildren())
			if (child->getName() == name)
				return *child;
		FAIL("Missing component " << name.toStdString());
		return editor;
	};
	auto& primary = find("Primary");
	auto& shaping = find("Shaping");
	auto& bands = find("Band Mix");
	auto& crossovers = find("Crossovers");
	REQUIRE(primary.getY() == bands.getY());
	REQUIRE(primary.getBottom() == bands.getBottom());
	REQUIRE(shaping.getY() == crossovers.getY());
	REQUIRE(shaping.getBottom() == crossovers.getBottom());
	for (auto* panel : { &primary, &shaping, &bands, &crossovers })
	{
		const auto children = panel->getChildren();
		REQUIRE(children.getFirst()->getX() == panel->getWidth() - children.getLast()->getRight());
		for (auto* child : children)
		{
			auto* rotary = dynamic_cast<vekt::ui::RotaryControl*>(child);
			REQUIRE(rotary != nullptr);
			REQUIRE(rotary->getHeight() == 140);
			REQUIRE(rotary->getSlider().getBounds().getCentreY() == 47);
			REQUIRE(rotary->getY() == primary.getChildren().getFirst()->getY());
			REQUIRE(rotary->getChildComponent(1)->getY() == 94);
			REQUIRE(rotary->getChildComponent(2)->getY() == 116);
		}
	}
	const auto& saturation = find("Saturation");
	const auto& overdrive = find("Overdrive");
	const auto& distortion = find("Distortion");
	const auto& circuitFuzz = find("Circuit Fuzz");
	const auto& gatedFuzz = find("Gated Fuzz");
	REQUIRE(saturation.getX() == primary.getX());
	REQUIRE(gatedFuzz.getRight() <= find("I/O").getRight());
	REQUIRE(overdrive.getX() - saturation.getRight() == 8);
	REQUIRE(distortion.getX() - overdrive.getRight() == 8);
	REQUIRE(gatedFuzz.getX() - distortion.getRight() == 8);
	REQUIRE(circuitFuzz.getX() - gatedFuzz.getRight() == 8);

	processor.setCurrentProgram(14); // Transient Smash: Gated Fuzz → Distortion → Saturation → Overdrive → Circuit Fuzz.
	editor.resized();
	REQUIRE(gatedFuzz.getX() == primary.getX());
	REQUIRE(distortion.getX() - gatedFuzz.getRight() == 8);
	REQUIRE(saturation.getX() - distortion.getRight() == 8);
	REQUIRE(overdrive.getX() - saturation.getRight() == 8);
	REQUIRE(circuitFuzz.getX() - overdrive.getRight() == 8);

	// Opt-in render artefact for visual review; normal test runs do not write files.
	if (const auto* path = std::getenv("VEKT_EDITOR_SNAPSHOT"))
	{
		editor.setSize(1120, 700);
		writeSnapshot(editor, path);
	}

	juce::Button* settings = nullptr;
	juce::Component* panel = nullptr;
	for (auto* child : editor.getContent().getChildren())
	{
		if (auto* button = dynamic_cast<juce::Button*>(child))
			if (button->getButtonText() == "Settings")
				settings = button;
		if (auto* candidate = dynamic_cast<vekt::ui::Panel*>(child))
			if (!candidate->isVisible())
				panel = candidate;
	}
	REQUIRE(settings != nullptr);
	REQUIRE(panel != nullptr);
	settings->setToggleState(true, juce::dontSendNotification);
	settings->onClick();
	REQUIRE(panel->isVisible());
	checkVisibleBounds(editor.getContent());
	settings->setToggleState(false, juce::dontSendNotification);
	settings->onClick();
	REQUIRE_FALSE(panel->isVisible());

	vekt::preset_ui::PresetBrowser* browser = nullptr;
	for (auto* child : editor.getContent().getChildren())
		if (auto* candidate = dynamic_cast<vekt::preset_ui::PresetBrowser*>(child)) browser = candidate;
	REQUIRE(browser != nullptr);
	browser->refresh();
	browser->setVisible(true);
	for (const auto width : { 1120, 1680, 2240 })
	{
		editor.setSize(width, width * 10 / 16);
		editor.resized();
		checkVisibleBounds(editor.getContent());
	}
}

TEST_CASE("Mono editor presents symmetric oscillator controls without overlap", "[processor][ui]")
{
	juce::ScopedJuceInitialiser_GUI initialiseJuce;
	vekt::mono::PluginProcessor processor;
	vekt::mono::PluginEditor editor(processor);
	const auto find = [&](const juce::String& name) -> juce::Component&
	{
		for (auto* child : editor.getContent().getChildren())
			if (child->getName() == name)
				return *child;
		FAIL("Missing component " << name.toStdString());
		return editor;
	};
	juce::Rectangle<int> oscillatorSliderBounds;
	const auto checkSourceControl = [&](const char* panelName, const juce::String& name, std::vector<juce::Rectangle<int>>& panelBounds)
	{
		bool found = false;
		for (auto* child : find(panelName).getChildren())
			if (auto* rotary = dynamic_cast<vekt::ui::RotaryControl*>(child); rotary != nullptr && rotary->getName() == name)
			{
				found = true;
				if (oscillatorSliderBounds.isEmpty()) oscillatorSliderBounds = rotary->getSlider().getBounds();
				REQUIRE(rotary->getSlider().getBounds() == oscillatorSliderBounds);
				REQUIRE(rotary->getSlider().getName() == name);
				if (name.endsWith("Morph")) REQUIRE(static_cast<bool>(rotary->getSlider().getProperties()["waveformGuide"]));
				REQUIRE(rotary->getHeight() == vekt::ui::RotaryControl::heightFor(vekt::ui::RotaryControl::Size::compact));
				for (const auto bounds : panelBounds) REQUIRE_FALSE(bounds.intersects(rotary->getBounds()));
				panelBounds.push_back(rotary->getBounds());
				if (name.endsWith("Octave")) REQUIRE(rotary->getSlider().getTooltip().contains("two octaves"));
				if (name.endsWith("Fine")) REQUIRE(rotary->getSlider().getTooltip().contains("cents"));
			}
		INFO("Panel " << panelName << " control " << name.toStdString());
		REQUIRE(found);
	};
	for (const auto* panelName : { "Osc 1", "Osc 2", "Osc 3" })
	{
		std::vector<juce::Rectangle<int>> panelBounds;
		for (const auto* control : { "Octave", "Fine", "Morph", "Width", "Level" })
			checkSourceControl(panelName, juce::String(panelName) + " " + control, panelBounds);
	}
	auto* noiseLevel = findRotary(find("Noise"), "Noise Level");
	REQUIRE(noiseLevel != nullptr);
	REQUIRE(noiseLevel->getSlider().getTooltip().contains("Noise mixer level"));
	bool foundNoiseType = false;
	for (auto* child : find("Noise").getChildren())
		if (auto* box = dynamic_cast<juce::ComboBox*>(child); box != nullptr && box->getNumItems() == 3 && box->getItemText(1) == "White")
			foundNoiseType = true;
	REQUIRE(foundNoiseType);
	REQUIRE(editor.getLogicalWidth() == vekt::mono::PluginEditor::editorWidth);
	for (const auto scale : { 1.0, 1.5, 2.0 })
	{
		editor.setSize(juce::roundToInt(editor.getLogicalWidth() * scale), juce::roundToInt(editor.getLogicalHeight() * scale));
		checkVisibleBounds(editor.getContent());
	}
	auto& ioPanel = find("I/O");
	auto* outputMeter = findMeter(ioPanel, "OUT");
	REQUIRE(outputMeter != nullptr);
	REQUIRE(outputMeter->getWidth() >= 36);
	REQUIRE(findMeter(ioPanel, "IN") == nullptr);
	bool foundOutputFader = false;
	for (auto* child : ioPanel.getChildren())
		if (auto* slider = dynamic_cast<juce::Slider*>(child); slider != nullptr && slider->getName() == "Master Output")
			foundOutputFader = true;
	REQUIRE(foundOutputFader);
	bool foundVoicePan = false;
	for (auto* child : find("Voice").getChildren())
		if (auto* rotary = dynamic_cast<vekt::ui::RotaryControl*>(child); rotary != nullptr && rotary->getName() == "Voice Pan")
		{
			foundVoicePan = true;
			REQUIRE(rotary->getSlider().getTooltip().contains("round-robin"));
		}
	REQUIRE(foundVoicePan);
	const std::array ladderTooltips {
		std::pair { "Cutoff", "exponentially" },
		std::pair { "Resonance", "natural bass loss" },
		std::pair { "Key Track", "one octave" },
		std::pair { "Env Amt", "octave pitch space" },
		std::pair { "Drive", "nonlinear filter" },
		std::pair { "Mode", "Notch" }
	};
	for (const auto& [controlName, expectedText] : ladderTooltips)
	{
		bool found = false;
		for (auto* child : find("Filter").getChildren())
			if (auto* rotary = dynamic_cast<vekt::ui::RotaryControl*>(child); rotary != nullptr && rotary->getName() == controlName)
			{
				found = true;
				REQUIRE(rotary->getSlider().getTooltip().contains(expectedText));
			}
		REQUIRE(found);
	}
	for (const auto* panelName : { "Osc 1", "Osc 2", "Osc 3", "Noise", "Filter", "Voice", "I/O", "Amp ADSR", "Filter ADSR", "Performance" })
	{
		auto& panel = find(panelName);
		for (int first = 0; first < panel.getNumChildComponents(); ++first)
			for (int second = first + 1; second < panel.getNumChildComponents(); ++second)
			{
				auto* firstChild = panel.getChildComponent(first);
				auto* secondChild = panel.getChildComponent(second);
				INFO(panelName << ": " << firstChild->getName().toStdString() << " / " << secondChild->getName().toStdString());
				REQUIRE_FALSE(firstChild->getBounds().intersects(secondChild->getBounds()));
			}
	}
	if (const auto* path = std::getenv("VEKT_MONO_SNAPSHOT"))
	{
		editor.resized();
		writeSnapshot(editor, path);
	}
}

TEST_CASE("Mono uses a secondary arc only for filter controls", "[processor][ui]")
{
	juce::ScopedJuceInitialiser_GUI initialiseJuce;
	vekt::mono::PluginProcessor processor;
	vekt::mono::PluginEditor editor(processor);
	const auto accent = juce::Colour::fromRGB(123, 191, 173);
	for (auto* panel : editor.getContent().getChildren())
	{
		const auto filterGroup = panel->getName() == "Filter" || panel->getName() == "Filter ADSR";
		for (auto* child : panel->getChildren())
		{
			auto* rotary = dynamic_cast<vekt::ui::RotaryControl*>(child);
			if (rotary == nullptr) continue;
			auto& slider = rotary->getSlider();
			INFO("Panel " << panel->getName().toStdString() << " control " << rotary->getName().toStdString());
			REQUIRE(slider.isColourSpecified(juce::Slider::rotarySliderFillColourId) == filterGroup);
			if (filterGroup) REQUIRE(slider.findColour(juce::Slider::rotarySliderFillColourId) == accent);
		}
	}
}

TEST_CASE("Mono quality menu exposes four selectable factors", "[mono][processor][ui][quality]")
{
	juce::ScopedJuceInitialiser_GUI initialiseJuce;
	vekt::mono::PluginProcessor processor;
	vekt::mono::PluginEditor editor(processor);
	auto& performance = [&editor]() -> juce::Component&
	{
		for (auto* child : editor.getContent().getChildren())
			if (child->getName() == "Performance") return *child;
		FAIL("Missing Performance panel");
		return editor;
	}();
	juce::ComboBox* quality = nullptr;
	for (auto* child : performance.getChildren())
		if (auto* box = dynamic_cast<juce::ComboBox*>(child);
			box != nullptr && box->getTooltip().contains("uses minimum-phase IIR"))
			quality = box;
	REQUIRE(quality != nullptr);
	REQUIRE(quality->getNumItems() == 4);
	for (int index = 0; index < 4; ++index)
		REQUIRE(quality->getItemText(index) == juce::String(1 << index) + "x");
	REQUIRE(quality->getSelectedItemIndex() == 0); // 1x is the default
	quality->setSelectedItemIndex(3, juce::sendNotificationSync);
	const auto* parameter = dynamic_cast<juce::AudioParameterChoice*>(
		processor.getParameters().getParameter(vekt::mono::parameters::quality));
	REQUIRE(parameter != nullptr);
	REQUIRE(parameter->getIndex() == 3);
}

TEST_CASE("Mono editor reports the active quality without a preview engine", "[mono][processor][ui][quality]")
{
	juce::ScopedJuceInitialiser_GUI initialiseJuce;
	const auto hasLabel = [](juce::Component& root, const juce::String& expected)
	{
		for (auto* child : root.getChildren())
			if (const auto* label = dynamic_cast<juce::Label*>(child);
				label != nullptr && label->getText().contains(expected)) return true;
		return false;
	};
	vekt::mono::PluginProcessor ordinary;
	ordinary.prepareToPlay(48'000.0, 128);
	vekt::mono::PluginEditor ordinaryEditor(ordinary);
	REQUIRE(hasLabel(ordinaryEditor.getContent(), "VEKT  MONO"));
	REQUIRE(hasLabel(ordinaryEditor.getContent(), "Quality: 1x"));
	vekt::mono::PluginProcessor high;
	auto* highQuality = high.getParameters().getParameter(vekt::mono::parameters::quality);
	REQUIRE(highQuality != nullptr);
	highQuality->setValueNotifyingHost(highQuality->convertTo0to1(3.0f));
	high.prepareToPlay(48'000.0, 128);
	vekt::mono::PluginEditor highEditor(high);
	REQUIRE(hasLabel(highEditor.getContent(), "Quality: 8x FIR"));
}

TEST_CASE("Mono Q compensation checkbox binds the default-off sound parameter", "[mono][processor][ui][qcomp]")
{
	juce::ScopedJuceInitialiser_GUI initialiseJuce;
	vekt::mono::PluginProcessor processor;
	vekt::mono::PluginEditor editor(processor);
	auto* button = findNamedButton(editor.getContent(), "Q Compensation");
	REQUIRE(button != nullptr);
	REQUIRE(button->isVisible());
	REQUIRE_FALSE(button->getToggleState());
	auto* parameter = processor.getParameters().getParameter(vekt::mono::parameters::filterQCompensation);
	REQUIRE(parameter != nullptr);
	button->setToggleState(true, juce::sendNotificationSync);
	REQUIRE(parameter->getValue() == Catch::Approx(1.0f));
	parameter->setValueNotifyingHost(0.0f);
	REQUIRE_FALSE(button->getToggleState());
	for (const auto width : { 1120, 1680, 2240 })
	{
		editor.setSize(width, width * 10 / 16);
		checkVisibleBounds(editor.getContent());
		for (auto* sibling : button->getParentComponent()->getChildren())
			if (sibling != button) REQUIRE_FALSE(button->getBounds().intersects(sibling->getBounds()));
	}
}

TEST_CASE("Mono Filter Type tabs select the filter and disable the Ladder-only toggles", "[mono][processor][ui][filter-type]")
{
	juce::ScopedJuceInitialiser_GUI initialiseJuce;
	vekt::mono::PluginProcessor processor;
	vekt::mono::PluginEditor editor(processor);
	auto* ladder = findNamedButton(editor.getContent(), "Filter Type Ladder");
	auto* svf = findNamedButton(editor.getContent(), "Filter Type SVF");
	auto* qCompensation = findNamedButton(editor.getContent(), "Q Compensation");
	REQUIRE(ladder != nullptr);
	REQUIRE(svf != nullptr);
	REQUIRE(qCompensation != nullptr);
	auto* parameter = processor.getParameters().getParameter(vekt::mono::parameters::filterType);
	REQUIRE(parameter != nullptr);
	// Default: Ladder lit, its toggles enabled.
	REQUIRE(ladder->getToggleState());
	REQUIRE_FALSE(svf->getToggleState());
	REQUIRE(qCompensation->isEnabled());
	// Clicking SVF selects it; the Ladder-only toggles stay visible but disabled, so the layout does not move.
	// What a click runs (triggerClick() would post it asynchronously).
	svf->onClick();
	REQUIRE(parameter->getValue() == Catch::Approx(1.0f));
	REQUIRE(svf->getToggleState());
	REQUIRE_FALSE(ladder->getToggleState());
	REQUIRE(qCompensation->isVisible());
	REQUIRE_FALSE(qCompensation->isEnabled());
	if (const auto* path = std::getenv("VEKT_MONO_SNAPSHOT_SVF")) writeSnapshot(editor, path);
	// A parameter change from elsewhere (preset, automation, undo) shows on the tabs; on the message thread the
	// attachment updates synchronously.
	parameter->setValueNotifyingHost(0.0f);
	REQUIRE(ladder->getToggleState());
	REQUIRE_FALSE(svf->getToggleState());
	REQUIRE(qCompensation->isEnabled());
	// The header row fits beside the title at every editor size.
	for (const auto width : { 1120, 1680, 2240 })
	{
		editor.setSize(width, width * 10 / 16);
		checkVisibleBounds(editor.getContent());
		for (auto* tab : { ladder, svf })
			for (auto* sibling : tab->getParentComponent()->getChildren())
				if (sibling != tab) REQUIRE_FALSE(tab->getBounds().intersects(sibling->getBounds()));
	}
}

TEST_CASE("Mono Resonance knob writes its full range to the processor", "[mono][processor][ui]")
{
	juce::ScopedJuceInitialiser_GUI initialiseJuce;
	vekt::mono::PluginProcessor processor;
	vekt::mono::PluginEditor editor(processor);
	auto* resonance = findRotary(editor.getContent(), "Resonance");
	REQUIRE(resonance != nullptr);
	auto& slider = resonance->getSlider();
	REQUIRE(slider.getMinimum() == Catch::Approx(0.0));
	REQUIRE(slider.getMaximum() == Catch::Approx(100.0));
	slider.setValue(slider.getMaximum(), juce::sendNotificationSync);
	REQUIRE(slider.getValue() == Catch::Approx(100.0));
	REQUIRE(processor.getParameters().getRawParameterValue(vekt::mono::parameters::filterResonance)->load()
		== Catch::Approx(100.0f));
}

TEST_CASE("Mono LFO panel shows one LFO at a time with every destination", "[mono][processor][ui][lfo]")
{
	juce::ScopedJuceInitialiser_GUI initialiseJuce;
	vekt::mono::PluginProcessor processor;
	vekt::mono::PluginEditor editor(processor);
	juce::Component* lfoPanel {};
	for (auto* child : editor.getContent().getChildren())
		if (child->getName() == "LFO") lfoPanel = child;
	REQUIRE(lfoPanel != nullptr);
	REQUIRE(editor.getContent().getLocalBounds().contains(lfoPanel->getBounds()));
	const auto find = [&](const juce::String& name) -> juce::Component*
	{
		for (auto* child : lfoPanel->getChildren())
			if (child->getName() == name) return child;
		return nullptr;
	};
	const auto checkShown = [&](int shown)
	{
		for (int number = 1; number <= 2; ++number)
			for (const auto* control : { "Shape", "Mode", "Amount", "Rate", "Osc 2 Width", "Spread" })
			{
				const auto name = "LFO " + juce::String(number) + " " + control;
				INFO(name.toStdString());
				auto* component = find(name);
				REQUIRE(component != nullptr);
				REQUIRE(component->isVisible() == (number == shown));
			}
		// Visible controls never overlap.
		std::vector<juce::Component*> visible;
		for (auto* child : lfoPanel->getChildren())
			if (child->isVisible()) visible.push_back(child);
		for (std::size_t first = 0; first < visible.size(); ++first)
			for (std::size_t second = first + 1; second < visible.size(); ++second)
			{
				INFO(visible[first]->getName().toStdString() << " / " << visible[second]->getName().toStdString());
				REQUIRE_FALSE(visible[first]->getBounds().intersects(visible[second]->getBounds()));
			}
		for (const auto scale : { 1.0, 2.0 })
		{
			editor.setSize(juce::roundToInt(editor.getLogicalWidth() * scale), juce::roundToInt(editor.getLogicalHeight() * scale));
			checkVisibleBounds(editor.getContent());
		}
	};
	checkShown(1);
	dynamic_cast<juce::Button*>(find("LFO 2 Tab"))->setToggleState(true, juce::sendNotificationSync);
	checkShown(2);
	REQUIRE_FALSE(dynamic_cast<juce::Button*>(find("LFO 1 Tab"))->getToggleState());
	REQUIRE(dynamic_cast<juce::Button*>(find("LFO 2 Tab"))->getToggleState());

	// Every destination has a bipolar slider bound to its depth parameter, reset by double-click to zero.
	const std::array destinations { "Osc 1 Pitch", "Osc 2 Pitch", "Osc 3 Pitch", "Osc 1 Morph", "Osc 2 Morph", "Osc 3 Morph",
		"Osc 1 Width", "Osc 2 Width", "Osc 3 Width", "Osc 1 Level", "Osc 2 Level", "Osc 3 Level",
		"Filter", "Amp", "Drive", "Noise", "Detune", "Spread", "Filter Mode" };
	for (std::size_t lfo = 0; lfo < vekt::mono::parameters::lfos.size(); ++lfo)
	{
		const auto ids = vekt::mono::parameters::lfos[lfo].depths();
		static_assert(ids.size() == destinations.size());
		for (std::size_t depth = 0; depth < destinations.size(); ++depth)
		{
			const auto name = "LFO " + juce::String(static_cast<int>(lfo) + 1) + " " + destinations[depth];
			INFO(name.toStdString());
			auto* slider = dynamic_cast<juce::Slider*>(find(name));
			REQUIRE(slider != nullptr);
			REQUIRE(static_cast<bool>(slider->getProperties()["bipolar"]));
			REQUIRE(slider->getDoubleClickReturnValue() == Catch::Approx(0.0));
			slider->setValue(slider->getMaximum(), juce::sendNotificationSync);
			auto* parameter = dynamic_cast<juce::RangedAudioParameter*>(processor.getParameters().getParameter(ids[depth]));
			REQUIRE(processor.getParameters().getRawParameterValue(ids[depth])->load()
				== Catch::Approx(parameter->getNormalisableRange().end));
		}
	}

	// Sync swaps the Hz rate knob for a note-division knob in the same place.
	auto* sync = dynamic_cast<juce::Button*>(find("LFO 2 Sync"));
	REQUIRE(sync != nullptr);
	REQUIRE(find("LFO 2 Rate")->isVisible());
	REQUIRE_FALSE(find("LFO 2 Division")->isVisible());
	sync->setToggleState(true, juce::sendNotificationSync);
	REQUIRE(processor.getParameters().getRawParameterValue(vekt::mono::parameters::lfos[1].sync)->load() == 1.0f);
	REQUIRE_FALSE(find("LFO 2 Rate")->isVisible());
	REQUIRE(find("LFO 2 Division")->isVisible());
	REQUIRE(find("LFO 2 Division")->getBounds() == find("LFO 2 Rate")->getBounds());
	// Readouts show the parameter's own text from the start, not the slider's raw default formatting.
	REQUIRE(dynamic_cast<vekt::ui::RotaryControl*>(find("LFO 2 Phase"))->getSlider().getTextFromValue(0.0) == "0.0");
	for (auto* child : find("LFO 2 Phase")->getChildren())
		if (auto* label = dynamic_cast<juce::Label*>(child); label != nullptr && label->getName().endsWith("value"))
			REQUIRE(label->getText() == "0.0");
	if (const auto* path = std::getenv("VEKT_MONO_LFO_SNAPSHOT"))
	{
		editor.setSize(editor.getLogicalWidth(), editor.getLogicalHeight());
		writeSnapshot(editor, path);
	}
}

TEST_CASE("Mono vibrato panel sits beside Performance with its controls and controller meter", "[mono][processor][ui][vibrato]")
{
	juce::ScopedJuceInitialiser_GUI initialiseJuce;
	vekt::mono::PluginProcessor processor;
	vekt::mono::PluginEditor editor(processor);
	juce::Component* vibrato {};
	juce::Component* performance {};
	for (auto* child : editor.getContent().getChildren())
	{
		if (child->getName() == "Vibrato") vibrato = child;
		if (child->getName() == "Performance") performance = child;
	}
	REQUIRE(vibrato != nullptr);
	REQUIRE(performance != nullptr);
	REQUIRE(vibrato->getY() == performance->getY());
	REQUIRE(vibrato->getHeight() == performance->getHeight());
	REQUIRE(editor.getContent().getLocalBounds().contains(vibrato->getBounds()));
	const auto find = [&](const juce::String& name) -> juce::Component*
	{
		for (auto* child : vibrato->getChildren())
			if (child->getName() == name) return child;
		return nullptr;
	};
	for (const auto* name : { "Vibrato Rate", "Vibrato Depth", "Vibrato Shape", "Vibrato Control" }) REQUIRE(find(name) != nullptr);
	auto* depth = dynamic_cast<vekt::ui::RotaryControl*>(find("Vibrato Depth"));
	depth->getSlider().setValue(80.0, juce::sendNotificationSync);
	REQUIRE(processor.getParameters().getRawParameterValue(vekt::mono::parameters::vibratoDepth)->load() == Catch::Approx(80.0f));
	for (int first = 0; first < vibrato->getNumChildComponents(); ++first)
		for (int second = first + 1; second < vibrato->getNumChildComponents(); ++second)
			REQUIRE_FALSE(vibrato->getChildComponent(first)->getBounds().intersects(vibrato->getChildComponent(second)->getBounds()));
	checkVisibleBounds(editor.getContent());
}

TEST_CASE("Mono shows how many voices are sounding beside the voice count", "[mono][processor][ui]")
{
	juce::ScopedJuceInitialiser_GUI initialiseJuce;
	vekt::mono::PluginProcessor processor;
	auto* mode = processor.getParameters().getParameter(vekt::mono::parameters::performanceMode);
	mode->setValueNotifyingHost(mode->convertTo0to1(0.0f));
	processor.prepareToPlay(48'000.0, 256);
	juce::AudioBuffer<float> buffer(2, 256);
	juce::MidiBuffer chord;
	for (const auto note : { 60, 64, 67 }) chord.addEvent(juce::MidiMessage::noteOn(1, note, 0.8f), 0);
	processor.processBlock(buffer, chord);
	REQUIRE(processor.getSoundingVoiceDisplay() == 3);

	vekt::mono::PluginEditor editor(processor);
	juce::Label* active {};
	for (auto* child : editor.getContent().getChildren())
		if (child->getName() == "Performance")
			for (auto* control : child->getChildren())
				if (control->getName() == "Active voices") active = dynamic_cast<juce::Label*>(control);
	REQUIRE(active != nullptr);
	REQUIRE(active->getText() == "3 active");

	processor.releaseResources();
	REQUIRE(processor.getSoundingVoiceDisplay() == 0);
}

TEST_CASE("Glimmer editor keeps stereo meters within its canvas", "[processor][ui]")
{
	juce::ScopedJuceInitialiser_GUI initialiseJuce;
	vekt::glimmer::PluginProcessor processor;
	vekt::glimmer::PluginEditor editor(processor);
	bool foundTone = false;
	for (auto* panel : editor.getContent().getChildren())
		for (auto* child : panel->getChildren())
			if (auto* control = dynamic_cast<vekt::ui::RotaryControl*>(child); control != nullptr && control->getName() == "Horn Tone")
			{
				foundTone = true;
				auto* value = dynamic_cast<juce::Label*>(control->getChildComponent(1));
				REQUIRE(value != nullptr);
				REQUIRE(value->getText() == "0.0 dB");
			}
	REQUIRE(foundTone);

	for (const auto width : { 1120, 1680, 2240 })
	{
		editor.setSize(width, width * 10 / 16);
		checkVisibleBounds(editor.getContent());
		checkMeterBounds(editor.getContent(), 28);
	}
	for (auto* panel : editor.getContent().getChildren())
		if (dynamic_cast<vekt::ui::Panel*>(panel) != nullptr)
			for (int first = 0; first < panel->getNumChildComponents(); ++first)
				for (int second = first + 1; second < panel->getNumChildComponents(); ++second)
				{
					auto* firstChild = panel->getChildComponent(first);
					auto* secondChild = panel->getChildComponent(second);
					INFO(firstChild->getName().toStdString() << " / " << secondChild->getName().toStdString());
					REQUIRE_FALSE(firstChild->getBounds().intersects(secondChild->getBounds()));
				}
	if (const auto* path = std::getenv("VEKT_GLIMMER_SNAPSHOT"))
	{
		editor.setSize(1120, 700);
		writeSnapshot(editor, path);
	}
	const auto findButton = [&](const juce::String& name) -> juce::Button&
	{
		if (auto* button = findNamedButton(editor.getContent(), name)) return *button;
		FAIL("Missing button " << name.toStdString());
		std::abort();
	};
	auto& classic = findButton("Classic model");
	auto& drum = findButton("Drum model");
	auto& wide = findButton("Wide model");
	auto& preset = findButton("Open preset browser");
	auto& undo = findButton("Undo");
	auto& redo = findButton("Redo");
	auto* history = dynamic_cast<vekt::ui::UndoRedoControls*>(undo.getParentComponent());
	REQUIRE(history != nullptr);
	auto* classicMode = dynamic_cast<vekt::ui::ModeButton*>(&classic);
	REQUIRE(classicMode != nullptr);
	REQUIRE(classic.getY() == 86);
	REQUIRE(drum.getY() == classic.getY());
	REQUIRE(wide.getY() == classic.getY());
	REQUIRE(classic.getWidth() > 300);
	REQUIRE_FALSE(classicMode->onDrag);
	REQUIRE_FALSE(classicMode->onDrop);
	REQUIRE(preset.getButtonText() == "Classic Chorale");
	REQUIRE(classic.getToggleState());
	juce::ignoreUnused(processor.getParameters().copyState());
	processor.getUndoManager().clearUndoHistory();
	history->refresh();
	REQUIRE_FALSE(undo.isEnabled());
	REQUIRE_FALSE(redo.isEnabled());
	wide.setToggleState(true, juce::sendNotificationSync);
	REQUIRE_FALSE(classic.getToggleState());
	REQUIRE_FALSE(drum.getToggleState());
	REQUIRE(wide.getToggleState());
	REQUIRE(processor.getParameters().getRawParameterValue(vekt::glimmer::parameters::cabinetModel)->load() == Catch::Approx(2));
	REQUIRE(preset.getButtonText() == "Classic Chorale *");
	juce::ignoreUnused(processor.getParameters().copyState());
	history->refresh();
	REQUIRE(undo.isEnabled());
	undo.onClick();
	REQUIRE(classic.getToggleState());
	REQUIRE_FALSE(wide.getToggleState());
	REQUIRE(preset.getButtonText() == "Classic Chorale");
	REQUIRE(redo.isEnabled());
	redo.onClick();
	REQUIRE(wide.getToggleState());
	REQUIRE(preset.getButtonText() == "Classic Chorale *");
	wide.onClick();
	REQUIRE(wide.getToggleState());
	processor.setCurrentProgram(2);
	REQUIRE(drum.getToggleState());
	REQUIRE_FALSE(wide.getToggleState());
	findButton("Next preset").onClick();
	REQUIRE(preset.getButtonText() == "Dynamic Drum");
	undo.onClick();
	REQUIRE(processor.getParameters().getRawParameterValue(vekt::glimmer::parameters::speedMode)->load() == Catch::Approx(1));
	REQUIRE(preset.getButtonText() == "Dynamic Drum *");
	redo.onClick();
	REQUIRE(processor.getParameters().getRawParameterValue(vekt::glimmer::parameters::speedMode)->load() == Catch::Approx(2));
	REQUIRE(preset.getButtonText() == "Dynamic Drum");
	findButton("Previous preset").onClick();
	REQUIRE(preset.getButtonText() == "Baffle Drive");
	for (auto* first : editor.getContent().getChildren())
		for (auto* second : editor.getContent().getChildren())
			if (first != second && first->isVisible() && second->isVisible())
				REQUIRE_FALSE(first->getBounds().intersects(second->getBounds()));
	preset.onClick();
	vekt::preset_ui::PresetBrowser* browser = nullptr;
	for (auto* child : editor.getContent().getChildren())
		if (auto* candidate = dynamic_cast<vekt::preset_ui::PresetBrowser*>(child)) browser = candidate;
	REQUIRE(browser != nullptr);
	REQUIRE(browser->isVisible());
	juce::ListBox* list = nullptr;
	juce::Button* load = nullptr;
	for (auto* child : browser->getChildren())
	{
		if (auto* box = dynamic_cast<juce::ComboBox*>(child)) box->setSelectedId(2, juce::sendNotificationSync);
		if (auto* rows = dynamic_cast<juce::ListBox*>(child)) list = rows;
		if (auto* button = dynamic_cast<juce::Button*>(child); button != nullptr && button->getButtonText() == "Load") load = button;
	}
	REQUIRE(list != nullptr);
	REQUIRE(load != nullptr);
	REQUIRE(list->getModel()->getNumRows() == 6);
	list->selectRow(5);
	load->onClick();
	REQUIRE(preset.getButtonText() == "Slow Panorama");
	REQUIRE(wide.getToggleState());
	for (const auto width : { 1120, 1680, 2240 })
	{
		editor.setSize(width, width * 10 / 16);
		checkVisibleBounds(editor.getContent());
	}
	if (const auto* path = std::getenv("VEKT_GLIMMER_BROWSER_SNAPSHOT"))
	{
		editor.setSize(1120, 700);
		writeSnapshot(editor, path);
	}
	browser->onClose();
	REQUIRE_FALSE(browser->isVisible());
}

TEST_CASE("Shared mode controls reorder only when enabled", "[processor][ui]")
{
	juce::ScopedJuceInitialiser_GUI initialiseJuce;
	for (const auto reorderable : { false, true })
	{
		juce::Component parent;
		parent.setSize(400, 100);
		vekt::ui::ModeButton mode("Model", reorderable);
		parent.addAndMakeVisible(mode);
		mode.setBounds(16, 16, 180, 48);
		mode.setMouseClickGrabsKeyboardFocus(false);
		int drags = 0;
		int drops = 0;
		mode.onDrag = [&](auto&, auto) { ++drags; };
		mode.onDrop = [&](auto&, auto) { ++drops; };
		const auto bounds = mode.getBounds();
		const auto event = [&](juce::Point<float> position, bool dragged)
		{
			return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), position,
				juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
				&mode, &mode, juce::Time::getCurrentTime(), { 10.0f, 10.0f }, juce::Time::getCurrentTime(), 1, dragged);
		};
		mode.mouseDown(event({ 10.0f, 10.0f }, false));
		mode.mouseDrag(event({ 50.0f, 10.0f }, true));
		mode.mouseUp(event({ 50.0f, 10.0f }, true));
		REQUIRE(drags == (reorderable ? 1 : 0));
		REQUIRE(drops == (reorderable ? 1 : 0));
		REQUIRE(mode.getBounds() == bounds);
		REQUIRE(mode.getAlpha() == Catch::Approx(1.0f));
	}
}

TEST_CASE("Shared editor history controls undo and redo parameter gestures", "[processor][ui]")
{
	juce::ScopedJuceInitialiser_GUI initialiseJuce;
	const auto checkHistory = [](auto& processor, auto& editor, const char* identifier)
	{
		auto* undo = findNamedButton(editor.getContent(), "Undo");
		auto* redo = findNamedButton(editor.getContent(), "Redo");
		REQUIRE(undo != nullptr);
		REQUIRE(redo != nullptr);
		auto* history = dynamic_cast<vekt::ui::UndoRedoControls*>(undo->getParentComponent());
		REQUIRE(history != nullptr);
		juce::ignoreUnused(processor.getParameters().copyState());
		processor.getUndoManager().clearUndoHistory();
		history->refresh();
		REQUIRE_FALSE(undo->isEnabled());
		REQUIRE_FALSE(redo->isEnabled());
		auto* parameter = processor.getParameters().getParameter(identifier);
		const auto original = parameter->getValue();
		parameter->beginChangeGesture();
		parameter->setValueNotifyingHost(parameter->convertTo0to1(6.0f));
		parameter->endChangeGesture();
		juce::ignoreUnused(processor.getParameters().copyState());
		history->refresh();
		REQUIRE(undo->isEnabled());
		REQUIRE(undo->getTooltip().startsWith("Undo"));
		parameter->setValueNotifyingHost(parameter->convertTo0to1(9.0f));
		undo->onClick();
		REQUIRE(parameter->getValue() == Catch::Approx(original));
		REQUIRE(redo->isEnabled());
		redo->onClick();
		REQUIRE(parameter->convertFrom0to1(parameter->getValue()) == Catch::Approx(9.0f));
		REQUIRE_FALSE(redo->isEnabled());
		undo->onClick();
		parameter->beginChangeGesture();
		parameter->setValueNotifyingHost(parameter->convertTo0to1(3.0f));
		parameter->endChangeGesture();
		juce::ignoreUnused(processor.getParameters().copyState());
		history->refresh();
		REQUIRE_FALSE(redo->isEnabled());
	};
	SECTION("Glimmer")
	{
		vekt::glimmer::PluginProcessor processor;
		vekt::glimmer::PluginEditor editor(processor);
		checkHistory(processor, editor, vekt::glimmer::parameters::inputGain);
	}
	SECTION("RAV")
	{
		vekt::rav::PluginProcessor processor;
		vekt::rav::PluginEditor editor(processor);
		checkHistory(processor, editor, vekt::rav::parameters::inputGain);
	}
}

TEST_CASE("Rotary numeric entry preserves precision and supports undo", "[ui]")
{
	juce::ScopedJuceInitialiser_GUI initialiseJuce;
	vekt::rav::PluginProcessor processor;
	vekt::rav::PluginEditor editor(processor);
	vekt::ui::RotaryControl* cutoff = nullptr;
	for (auto* panel : editor.getContent().getChildren())
		for (auto* child : panel->getChildren())
			if (child->getName() == "Mid-High")
				cutoff = dynamic_cast<vekt::ui::RotaryControl*>(child);
	REQUIRE(cutoff != nullptr);
	auto* field = dynamic_cast<juce::Label*>(cutoff->getChildComponent(1));
	REQUIRE(field != nullptr);
	auto* parameter = processor.getParameters().getParameter(vekt::rav::parameters::midHighCutoffHz);
	parameter->setValueNotifyingHost(parameter->convertTo0to1(2501.23f));
	juce::ignoreUnused(processor.getParameters().copyState());
	const auto original = cutoff->getSlider().getValue();
	field->onTextChange();
	REQUIRE(cutoff->getSlider().getValue() == original);
	field->setText("invalid", juce::sendNotificationSync);
	REQUIRE(cutoff->getSlider().getValue() == original);
	REQUIRE(field->getTooltip().isNotEmpty());
	field->setText("3 kHz", juce::sendNotificationSync);
	REQUIRE(cutoff->getSlider().getValue() == 3000.0);
	REQUIRE(field->getTooltip().isEmpty());
	// APVTS normally flushes parameter changes to its undoable tree on a timer.
	juce::ignoreUnused(processor.getParameters().copyState());
	REQUIRE(processor.getUndoManager().undo());
	REQUIRE(cutoff->getSlider().getValue() == original);
}
