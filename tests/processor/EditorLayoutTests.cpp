#include <vekt/rav/PluginEditor.h>
#include <vekt/rav/Parameters.h>
#include <vekt/glimmer/PluginEditor.h>
#include <vekt/mono/PluginEditor.h>
#include <vekt/flint/PluginEditor.h>
#include <vekt/dsp/OversamplingChoices.h>
#include <vekt/ui/QualitySettings.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdlib>
#include <vector>

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

// The dot shows the LFO a few milliseconds back (DisplayTimeline), so it is compared with the latest output within
// this much: the most a 1 Hz LFO moves in about 30 ms.
constexpr auto displayedLfoTolerance = 0.2;
// The LFO output a Cutoff dot implies, for a depth in octaves around a base cutoff.
double cutoffDotOutput(double cutoff, double base, double octaves) { return std::log2(cutoff / base) / octaves; }
constexpr auto blockSeconds = 512.0 / 48'000.0;

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

// Each product shows its output waveform in the I/O panel, clear of the other I/O controls.
void checkOutputScope(juce::Component& content, int minimumWidth, int minimumHeight)
{
	juce::Component* ioPanel = nullptr;
	for (auto* child : content.getChildren())
		if (child->getName() == "I/O") ioPanel = child;
	REQUIRE(ioPanel != nullptr);
	vekt::ui::Oscilloscope* scope = nullptr;
	for (auto* child : ioPanel->getChildren())
		if (auto* candidate = dynamic_cast<vekt::ui::Oscilloscope*>(child)) scope = candidate;
	REQUIRE(scope != nullptr);
	REQUIRE(scope->isVisible());
	REQUIRE(scope->getWidth() >= minimumWidth);
	REQUIRE(scope->getHeight() >= minimumHeight);
	for (auto* sibling : ioPanel->getChildren())
		if (sibling != scope && sibling->isVisible())
		{
			INFO("Overlaps " << sibling->getName().toStdString());
			REQUIRE_FALSE(sibling->getBounds().intersects(scope->getBounds()));
		}
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

TEST_CASE("Rav editor keeps its controls within the 16:10 canvas", "[processor][ui][rav]")
{
	juce::ScopedJuceInitialiser_GUI initialiseJuce;
	vekt::rav::PluginProcessor processor;
	vekt::rav::PluginEditor editor(processor);
	REQUIRE(editor.getWidth() == 1120);
	REQUIRE(editor.getHeight() == 700);
	REQUIRE(juce::exactlyEqual(editor.getConstrainer()->getFixedAspectRatio(), 1.6));

	for (const auto width : { 1120, 1680, 2240 })
	{
		editor.setSize(width, width * 10 / 16);
		checkVisibleBounds(editor.getContent());
		checkMeterBounds(editor.getContent(), 32);
		checkOutputScope(editor.getContent(), 200, 100);
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

TEST_CASE("Kobber editor presents symmetric oscillator controls without overlap", "[processor][ui][kobber]")
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
				if (name.endsWith("Morph"))
				{
					REQUIRE(static_cast<bool>(rotary->getSlider().getProperties()["waveformGuide"]));
					REQUIRE(static_cast<bool>(rotary->getSlider().getProperties()["endless"]));
				}
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
	checkOutputScope(editor.getContent(), 120, 60);
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

TEST_CASE("Kobber uses a secondary arc only for filter controls", "[processor][ui][kobber]")
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

namespace
{
// Every parameter menu an editor holds, hidden pages included, as its item texts. (The preset browser's folder menu is
// not a parameter's.)
std::vector<juce::StringArray> parameterMenus(juce::Component& editor)
{
	std::vector<juce::StringArray> menus;
	std::function<void(juce::Component&)> collect = [&](juce::Component& component)
	{
		if (dynamic_cast<vekt::preset_ui::PresetBrowser*>(&component) != nullptr) return;
		if (const auto* box = dynamic_cast<juce::ComboBox*>(&component))
		{
			juce::StringArray items;
			for (int item = 0; item < box->getNumItems(); ++item) items.add(box->getItemText(item));
			menus.push_back(items);
		}
		for (auto* child : component.getChildren()) collect(*child);
	};
	collect(editor);
	return menus;
}

// Every menu shows exactly some choice parameter's own list: none keeps a copy of its own.
void checkMenusListParameterChoices(juce::AudioProcessor& processor, const std::vector<juce::StringArray>& menus)
{
	std::vector<juce::StringArray> parameterChoices;
	for (auto* parameter : processor.getParameters())
		if (const auto* choice = dynamic_cast<juce::AudioParameterChoice*>(parameter)) parameterChoices.push_back(choice->choices);
	for (const auto& menu : menus)
	{
		CAPTURE(menu.joinIntoString(", "));
		REQUIRE(std::find(parameterChoices.begin(), parameterChoices.end(), menu) != parameterChoices.end());
	}
}
}

TEST_CASE("Kobber editor menus list their parameters' choices", "[kobber][processor][ui]")
{
	juce::ScopedJuceInitialiser_GUI initialiseJuce;
	vekt::mono::PluginProcessor processor;
	vekt::mono::PluginEditor editor(processor);
	const auto menus = parameterMenus(editor);
	REQUIRE(menus.size() >= 12);
	checkMenusListParameterChoices(processor, menus);
	REQUIRE(std::find(menus.begin(), menus.end(), juce::StringArray { "Last", "Low" }) != menus.end()); // Mono Priority
}

TEST_CASE("Rav and Glimmer editor menus list their parameters' choices", "[rav][glimmer][processor][ui]")
{
	juce::ScopedJuceInitialiser_GUI initialiseJuce;
	{
		vekt::rav::PluginProcessor processor;
		vekt::rav::PluginEditor editor(processor);
		const auto menus = parameterMenus(editor);
		REQUIRE(menus.size() >= 3); // Mode and the two quality menus
		checkMenusListParameterChoices(processor, menus);
	}
	vekt::glimmer::PluginProcessor processor;
	vekt::glimmer::PluginEditor editor(processor);
	const auto menus = parameterMenus(editor);
	REQUIRE(menus.size() >= 3); // Speed and the two quality menus
	checkMenusListParameterChoices(processor, menus);
}

TEST_CASE("Kobber quality menus offer the shared tracking and offline choices", "[kobber][processor][ui][quality]")
{
	juce::ScopedJuceInitialiser_GUI initialiseJuce;
	vekt::mono::PluginProcessor processor;
	vekt::mono::PluginEditor editor(processor);
	// The shared Settings pop-over (vekt::ui::QualitySettings), opened from the header, holds both menus.
	auto* settings = static_cast<vekt::ui::QualitySettings*>(nullptr);
	for (auto* child : editor.getContent().getChildren())
		if (auto* candidate = dynamic_cast<vekt::ui::QualitySettings*>(child)) settings = candidate;
	REQUIRE(settings != nullptr);
	REQUIRE_FALSE(settings->isVisible());
	auto& toggle = settings->getSettingsButton();
	REQUIRE(toggle.getParentComponent() == &editor.getContent());
	toggle.setToggleState(true, juce::dontSendNotification);
	toggle.onClick();
	REQUIRE(settings->isVisible());
	checkVisibleBounds(editor.getContent());
	// No quality menu is left in the Performance panel.
	for (auto* child : editor.getContent().getChildren())
		if (child->getName() == "Performance")
			for (auto* inner : child->getChildren())
				if (const auto* box = dynamic_cast<juce::ComboBox*>(inner))
					for (int item = 0; item < box->getNumItems(); ++item)
						REQUIRE(box->getItemText(item) != "16x FIR");
	const auto findBox = [settings](const juce::String& name) -> juce::ComboBox*
	{
		for (auto* child : settings->getChildren())
			if (auto* box = dynamic_cast<juce::ComboBox*>(child); box != nullptr && box->getName() == name) return box;
		return nullptr;
	};
	struct Menu { juce::String name; const char* identifier; juce::StringArray choices; int defaultIndex; };
	for (const auto& menu : { Menu { "Tracking quality", vekt::mono::parameters::trackingOversampling, vekt::dsp::trackingQualityChoices(), 0 },
			 Menu { "Offline quality", vekt::mono::parameters::offlineOversampling, vekt::dsp::offlineQualityChoices(), 2 } })
	{
		INFO(menu.name);
		auto* box = findBox(menu.name);
		REQUIRE(box != nullptr);
		REQUIRE(box->getNumItems() == menu.choices.size());
		for (int index = 0; index < menu.choices.size(); ++index)
			REQUIRE(box->getItemText(index) == menu.choices[index]);
		REQUIRE(box->getSelectedItemIndex() == menu.defaultIndex); // Tracking Off, Offline 4x FIR
		const auto highest = menu.choices.indexOf("16x FIR");
		REQUIRE(highest >= 0);
		box->setSelectedItemIndex(highest, juce::sendNotificationSync);
		const auto* parameter = dynamic_cast<juce::AudioParameterChoice*>(processor.getParameters().getParameter(menu.identifier));
		REQUIRE(parameter != nullptr);
		REQUIRE(parameter->getIndex() == highest);
	}
}

TEST_CASE("Kobber editor reports the active quality without a preview engine", "[kobber][processor][ui][quality]")
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
	REQUIRE(hasLabel(ordinaryEditor.getContent(), "Quality: Off"));
	vekt::mono::PluginProcessor high;
	auto* highQuality = high.getParameters().getParameter(vekt::mono::parameters::trackingOversampling);
	REQUIRE(highQuality != nullptr);
	highQuality->setValueNotifyingHost(highQuality->convertTo0to1(6.0f));
	high.prepareToPlay(48'000.0, 128);
	vekt::mono::PluginEditor highEditor(high);
	REQUIRE(hasLabel(highEditor.getContent(), "Quality: 16x FIR"));
}

TEST_CASE("Kobber Q compensation checkbox binds the default-off sound parameter", "[kobber][processor][ui][qcomp]")
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

TEST_CASE("Kobber Filter Type tabs select the filter and disable the Ladder-only toggles", "[kobber][processor][ui][filter-type]")
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
	REQUIRE(processor.getParameters().getRawParameterValue(vekt::mono::parameters::filterType)->load() == 1.0f);
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

TEST_CASE("Kobber filter tabs select the type in one undo step each, keep Mode and disable Q Comp for K35", "[kobber][processor][ui][filter-type][k35]")
{
	juce::ScopedJuceInitialiser_GUI initialiseJuce;
	vekt::mono::PluginProcessor processor;
	vekt::mono::PluginEditor editor(processor);
	auto* ladder = findNamedButton(editor.getContent(), "Filter Type Ladder");
	auto* svf = findNamedButton(editor.getContent(), "Filter Type SVF");
	auto* k35 = findNamedButton(editor.getContent(), "Filter Type K35");
	auto* qCompensation = findNamedButton(editor.getContent(), "Q Compensation");
	auto* mode = findRotary(editor.getContent(), "Mode");
	REQUIRE(ladder != nullptr);
	REQUIRE(svf != nullptr);
	REQUIRE(k35 != nullptr);
	REQUIRE(qCompensation != nullptr);
	REQUIRE(mode != nullptr);
	auto& state = processor.getParameters();
	const auto value = [&state](const char* identifier) { return state.getRawParameterValue(identifier)->load(); };
	auto& undo = processor.getUndoManager();
	// APVTS normally flushes parameter changes to its undoable tree on a timer; copyState() flushes now.
	const auto flush = [&state] { juce::ignoreUnused(state.copyState()); };
	// SVF, then K35: K35 lit, Mode enabled (its high-pass input), Q Comp disabled but visible.
	svf->onClick();
	flush();
	k35->onClick();
	flush();
	REQUIRE(value(vekt::mono::parameters::filterType) == 2.0f);
	REQUIRE(k35->getToggleState());
	REQUIRE_FALSE(svf->getToggleState());
	REQUIRE_FALSE(ladder->getToggleState());
	REQUIRE(mode->isVisible());
	REQUIRE(mode->isEnabled());
	REQUIRE_FALSE(qCompensation->isEnabled());
	if (const auto* path = std::getenv("VEKT_MONO_SNAPSHOT_K35")) writeSnapshot(editor, path);
	// Each click is one undo step.
	REQUIRE(undo.undo());
	REQUIRE(value(vekt::mono::parameters::filterType) == 1.0f);
	REQUIRE(svf->getToggleState());
	REQUIRE(mode->isEnabled());
	REQUIRE(undo.redo());
	REQUIRE(value(vekt::mono::parameters::filterType) == 2.0f);
	REQUIRE(k35->getToggleState());
	// Host automation moves the tabs.
	state.getParameter(vekt::mono::parameters::filterType)->setValueNotifyingHost(0.0f);
	flush();
	REQUIRE(ladder->getToggleState());
	REQUIRE_FALSE(k35->getToggleState());
	REQUIRE(qCompensation->isEnabled());
	// Three tabs and the shortened Q Comp toggle share the header without overlap at every size.
	for (const auto width : { 1120, 1680, 2240 })
	{
		editor.setSize(width, width * 10 / 16);
		checkVisibleBounds(editor.getContent());
		for (auto* control : { ladder, svf, k35, qCompensation })
			for (auto* sibling : control->getParentComponent()->getChildren())
				if (sibling != control) REQUIRE_FALSE(control->getBounds().intersects(sibling->getBounds()));
	}
}

TEST_CASE("Kobber Resonance knob writes its full range to the processor", "[kobber][processor][ui]")
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

TEST_CASE("Kobber LFO panel shows one LFO at a time with every destination", "[kobber][processor][ui][lfo]")
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

TEST_CASE("Kobber vibrato panel sits beside Performance with its controls and controller meter", "[kobber][processor][ui][vibrato]")
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
	// The controller bar is the on-screen mod wheel: a focusable slider on Vibrato Amount.
	auto* wheel = dynamic_cast<vekt::mono::VibratoWheel*>(find("Vibrato Control"));
	REQUIRE(wheel != nullptr);
	const auto amount = [&] { return processor.getParameters().getRawParameterValue(vekt::mono::parameters::vibratoAmount)->load(); };
	REQUIRE(wheel->getWantsKeyboardFocus());
	REQUIRE(wheel->getDoubleClickReturnValue() == 0.0);
	wheel->setValue(60.0, juce::sendNotificationSync);
	REQUIRE(amount() == Catch::Approx(60.0f));
	REQUIRE(wheel->keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
	REQUIRE(amount() == Catch::Approx(61.0f));
	REQUIRE(wheel->keyPressed(juce::KeyPress(juce::KeyPress::leftKey, juce::ModifierKeys::shiftModifier, 0)));
	REQUIRE(amount() == Catch::Approx(60.9f));
	// The bar spans the same width as its label above it.
	REQUIRE(wheel->getPositionOfValue(0.0) == Catch::Approx(156.0 - wheel->getX()));
	REQUIRE(wheel->getPositionOfValue(100.0) == Catch::Approx(336.0 - wheel->getX()));
	// Mouse: dragging from the left end to the middle sets half the amount.
	const auto y = static_cast<float>(wheel->getHeight()) * 0.5f;
	const auto at = [&](double value) { return juce::Point<float>(static_cast<float>(wheel->getPositionOfValue(value)), y); };
	auto source = juce::Desktop::getInstance().getMainMouseSource();
	const auto event = [&](juce::Point<float> position, juce::Point<float> down)
	{
		return juce::MouseEvent(source, position, {}, juce::MouseInputSource::defaultPressure, 0.0f, 0.0f, 0.0f, 0.0f,
			wheel, wheel, juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), 1, false);
	};
	wheel->mouseDown(event(at(0.0), at(0.0)));
	wheel->mouseDrag(event(at(50.0), at(0.0)));
	wheel->mouseUp(event(at(50.0), at(0.0)));
	REQUIRE(amount() == Catch::Approx(50.0f).margin(1.0f));
	for (int first = 0; first < vibrato->getNumChildComponents(); ++first)
		for (int second = first + 1; second < vibrato->getNumChildComponents(); ++second)
			REQUIRE_FALSE(vibrato->getChildComponent(first)->getBounds().intersects(vibrato->getChildComponent(second)->getBounds()));
	checkVisibleBounds(editor.getContent());
}

TEST_CASE("Kobber shows how many voices are sounding beside the voice count", "[kobber][processor][ui]")
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

TEST_CASE("Glimmer editor keeps stereo meters within its canvas", "[processor][ui][glimmer]")
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
		checkOutputScope(editor.getContent(), 100, 100);
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
	REQUIRE(list->getListBoxModel()->getNumRows() == 6);
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
	REQUIRE(juce::exactlyEqual(cutoff->getSlider().getValue(), original));
	field->setText("invalid", juce::sendNotificationSync);
	REQUIRE(juce::exactlyEqual(cutoff->getSlider().getValue(), original));
	REQUIRE(field->getTooltip().isNotEmpty());
	field->setText("3 kHz", juce::sendNotificationSync);
	REQUIRE(cutoff->getSlider().getValue() == 3000.0);
	REQUIRE(field->getTooltip().isEmpty());
	// APVTS normally flushes parameter changes to its undoable tree on a timer.
	juce::ignoreUnused(processor.getParameters().copyState());
	REQUIRE(processor.getUndoManager().undo());
	REQUIRE(juce::exactlyEqual(cutoff->getSlider().getValue(), original));
}

TEST_CASE("Kobber knobs show the range their LFO depths reach", "[kobber][processor][ui][lfo][modulation]")
{
	namespace parameters = vekt::mono::parameters;
	juce::ScopedJuceInitialiser_GUI initialiseJuce;
	vekt::mono::PluginProcessor processor;
	const auto set = [&](const char* identifier, float value)
	{
		auto* parameter = processor.getParameters().getParameter(identifier);
		parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
	};
	for (auto* parameter : processor.juce::AudioProcessor::getParameters())
		parameter->setValueNotifyingHost(parameter->getDefaultValue());
	{
		vekt::mono::PluginEditor editor(processor);
		for (const auto* name : { "Cutoff", "Osc 1 Morph", "Detune" })
		{
			INFO(name);
			auto* rotary = findRotary(editor.getContent(), name);
			REQUIRE(rotary != nullptr);
			REQUIRE_FALSE(rotary->getModulationRing().getModulation().has_value());
		}
	}

	set(parameters::filterCutoff, 1'000.0f);
	// LFO 1 bipolar at full Amount: one octave each way.
	set(parameters::lfos[0].filter, 1.0f);
	// LFO 2 unipolar at half Amount, pulling down: a further half octave below only.
	set(parameters::lfos[1].polarity, 1.0f);
	set(parameters::lfos[1].amount, 50.0f);
	set(parameters::lfos[1].filter, -1.0f);
	set(parameters::osc2Morph, 1.0f);
	set(parameters::lfos[0].morph[1], 10.0f);
	set(parameters::osc1Morph, 2.0f);
	set(parameters::lfos[1].morph[0], 100.0f);
	set(parameters::unisonDetune, 15.0f);
	set(parameters::lfos[0].detune, 20.0f);
	set(parameters::lfos[0].amp, 50.0f);
	vekt::mono::PluginEditor editor(processor);
	const auto modulation = [&](const char* name)
	{
		auto* rotary = findRotary(editor.getContent(), name);
		REQUIRE(rotary != nullptr);
		return rotary->getModulationRing().getModulation();
	};
	const auto cutoff = modulation("Cutoff");
	REQUIRE(cutoff.has_value());
	REQUIRE(cutoff->lowest == Catch::Approx(1'000.0 * std::exp2(-1.5)).epsilon(1.0e-4));
	REQUIRE(cutoff->highest == Catch::Approx(2'000.0).epsilon(1.0e-4));
	// 10 % Morph depth is 0.4 of the 0..4 cycle each way.
	const auto morph = modulation("Osc 2 Morph");
	REQUIRE(morph.has_value());
	REQUIRE(morph->lowest == Catch::Approx(0.6));
	REQUIRE(morph->highest == Catch::Approx(1.4));
	// 100 % depth at half Amount, unipolar, is half a turn upwards.
	const auto halfTurn = modulation("Osc 1 Morph");
	REQUIRE(halfTurn.has_value());
	REQUIRE(halfTurn->lowest == Catch::Approx(2.0));
	REQUIRE(halfTurn->highest == Catch::Approx(4.0));
	// Detune depth 20 % is +/-10 ct.
	const auto detune = modulation("Detune");
	REQUIRE(detune.has_value());
	REQUIRE(detune->lowest == Catch::Approx(5.0));
	REQUIRE(detune->highest == Catch::Approx(25.0));
	// Unmodulated knobs keep no ring.
	REQUIRE_FALSE(modulation("Resonance").has_value());
	REQUIRE_FALSE(modulation("Osc 3 Morph").has_value());
	if (const auto* path = std::getenv("VEKT_MONO_SNAPSHOT"))
	{
		editor.resized();
		writeSnapshot(editor, path);
	}
}

TEST_CASE("Kobber modulation dots follow the live LFO and fade when they move too fast to follow", "[kobber][processor][ui][lfo][modulation]")
{
	namespace parameters = vekt::mono::parameters;
	juce::ScopedJuceInitialiser_GUI initialiseJuce;
	// Peak speed is pi x rate x swing, as a share of the knob's travel: a +/-50 % level swing covers the whole knob.
	const auto& level = vekt::mono::lfoDestinations[vekt::mono::lfo_depth::level];
	const juce::NormalisableRange<float> percent { 0.0f, 100.0f };
	REQUIRE(vekt::mono::lfoPeakTravelPerSecond(level, percent, 50.0, 0.5f, vekt::mono::LfoPolarity::bipolar, 2.0f)
		== Catch::Approx(2.0 * juce::MathConstants<double>::pi));
	REQUIRE(vekt::mono::lfoPeakTravelPerSecond(level, percent, 50.0, -0.5f, vekt::mono::LfoPolarity::unipolar, 2.0f)
		== Catch::Approx(juce::MathConstants<double>::pi));

	vekt::mono::PluginProcessor processor;
	const auto set = [&](const char* identifier, float value)
	{
		auto* parameter = processor.getParameters().getParameter(identifier);
		parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
	};
	for (auto* parameter : processor.juce::AudioProcessor::getParameters())
		parameter->setValueNotifyingHost(parameter->getDefaultValue());
	set(parameters::filterCutoff, 1'000.0f);
	set(parameters::lfos[0].filter, 1.0f);
	set(parameters::lfos[0].rate, 1.0f);
	set(parameters::ampSustain, 100.0f);
	const auto cutoffModulation = [&]
	{
		vekt::mono::PluginEditor editor(processor);
		auto* rotary = findRotary(editor.getContent(), "Cutoff");
		REQUIRE(rotary != nullptr);
		return rotary->getModulationRing().getModulation();
	};
	// Silent: the range shows without a dot.
	auto modulation = cutoffModulation();
	REQUIRE(modulation.has_value());
	REQUIRE_FALSE(modulation->current.has_value());

	processor.prepareToPlay(48'000.0, 512);
	juce::AudioBuffer<float> buffer(2, 512);
	juce::MidiBuffer note;
	note.addEvent(juce::MidiMessage::noteOn(1, 57, 0.8f), 0);
	processor.processBlock(buffer, note);
	juce::MidiBuffer none;
	for (int block = 0; block < 20; ++block) processor.processBlock(buffer, none);
	const auto output = processor.getLfoDisplayValue(0);
	REQUIRE(std::abs(output) > 0.1f);
	modulation = cutoffModulation();
	REQUIRE(modulation->current.has_value());
	REQUIRE(cutoffDotOutput(*modulation->current, 1'000.0, 1.0) == Catch::Approx(output).margin(displayedLfoTolerance));
	REQUIRE(modulation->currentOpacity == Catch::Approx(1.0f));

	// What counts is how fast the dot moves, not the rate alone. A one-octave sweep is a small part of Cutoff's
	// travel, so even at 15 Hz its dot stays, at full size and brightness.
	set(parameters::lfos[0].rate, 15.0f);
	processor.processBlock(buffer, none);
	REQUIRE(cutoffModulation()->currentOpacity == Catch::Approx(1.0f));
	// A four-octave sweep covers most of it: full at 3 Hz, handing over at 5 Hz, gone at 10.
	set(parameters::lfos[0].filter, 4.0f);
	set(parameters::lfos[0].rate, 3.0f);
	processor.processBlock(buffer, none);
	REQUIRE(cutoffModulation()->currentOpacity == Catch::Approx(1.0f));
	set(parameters::lfos[0].rate, 5.0f);
	processor.processBlock(buffer, none);
	const auto handingOver = cutoffModulation();
	const auto fading = handingOver->currentOpacity;
	REQUIRE(fading > 0.0f);
	REQUIRE(fading < 1.0f);
	// Even alone, the LFO's band is already growing while its dot fades: never a moment with neither.
	REQUIRE(handingOver->blur.has_value());
	// The same rate with a deeper sweep moves the dot faster, so it hands over further.
	set(parameters::lfos[0].filter, 5.0f);
	processor.processBlock(buffer, none);
	modulation = cutoffModulation();
	REQUIRE(modulation->current.has_value());
	REQUIRE(modulation->currentOpacity < fading);
	set(parameters::lfos[0].filter, 4.0f);
	set(parameters::lfos[0].rate, 10.0f);
	processor.processBlock(buffer, none);
	modulation = cutoffModulation();
	REQUIRE(modulation.has_value());
	REQUIRE_FALSE(modulation->current.has_value());
	if (const auto* path = std::getenv("VEKT_MONO_SNAPSHOT"))
	{
		set(parameters::lfos[0].rate, 1.0f);
		processor.processBlock(buffer, none);
		vekt::mono::PluginEditor editor(processor);
		editor.resized();
		writeSnapshot(editor, path);
	}
	processor.releaseResources();
}

TEST_CASE("Kobber modulation overflows the knob only where the voice does", "[kobber][processor][ui][lfo][modulation]")
{
	namespace parameters = vekt::mono::parameters;
	juce::ScopedJuceInitialiser_GUI initialiseJuce;
	vekt::mono::PluginProcessor processor;
	const auto set = [&](const char* identifier, float value)
	{
		auto* parameter = processor.getParameters().getParameter(identifier);
		parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
	};
	for (auto* parameter : processor.juce::AudioProcessor::getParameters())
		parameter->setValueNotifyingHost(parameter->getDefaultValue());
	// Pitch is unbounded: at Octave +2 an upward unipolar LFO keeps raising it, an octave past the knob here.
	set(parameters::osc1Octave, 2.0f);
	set(parameters::lfos[0].polarity, 1.0f);
	set(parameters::lfos[0].pitch[0], 12.0f);
	// Cutoff reaches past 20 kHz (an octave up from 18 kHz, unipolar), but only to the voice's own limit (0.45 x 48 kHz).
	set(parameters::filterCutoff, 18'000.0f);
	set(parameters::lfos[0].filter, 1.0f);
	// Level stops at 100 %, exactly where the knob does.
	set(parameters::osc2Level, 100.0f);
	set(parameters::lfos[0].level[1], 50.0f);
	set(parameters::lfos[0].rate, 1.0f);
	set(parameters::ampSustain, 100.0f);
	processor.setRateAndBufferSizeDetails(48'000.0, 512);
	processor.prepareToPlay(48'000.0, 512);
	juce::AudioBuffer<float> buffer(2, 512);
	juce::MidiBuffer note;
	note.addEvent(juce::MidiMessage::noteOn(1, 57, 0.8f), 0);
	processor.processBlock(buffer, note);
	juce::MidiBuffer none;
	// A quarter cycle: the unipolar LFO near its peak.
	for (int block = 0; block < 23; ++block) processor.processBlock(buffer, none);
	REQUIRE(processor.getLfoDisplayValue(0) > 0.9f);

	vekt::mono::PluginEditor editor(processor);
	const auto ring = [&](const char* name) -> const vekt::ui::ModulationRing&
	{
		auto* rotary = findRotary(editor.getContent(), name);
		REQUIRE(rotary != nullptr);
		return rotary->getModulationRing();
	};
	const auto& pitch = ring("Osc 1 Octave");
	REQUIRE(pitch.getModulation()->lowest == Catch::Approx(2.0));
	REQUIRE(pitch.getModulation()->highest == Catch::Approx(3.0));
	REQUIRE(pitch.overflow().above);
	REQUIRE(pitch.isCurrentBeyondTravel());
	REQUIRE(*pitch.getModulation()->current > 2.9);

	const auto& cutoff = ring("Cutoff");
	REQUIRE(cutoff.getModulation()->lowest == Catch::Approx(18'000.0));
	REQUIRE(cutoff.getModulation()->highest == Catch::Approx(21'600.0));
	REQUIRE(cutoff.overflow().above);
	REQUIRE_FALSE(cutoff.overflow().below);

	const auto& level = ring("Osc 2 Level");
	REQUIRE(level.getModulation()->highest == Catch::Approx(100.0));
	REQUIRE_FALSE(level.overflow().above);
	REQUIRE_FALSE(level.isCurrentBeyondTravel());
	if (const auto* path = std::getenv("VEKT_MONO_SNAPSHOT"))
	{
		editor.resized();
		writeSnapshot(editor, path);
	}
	processor.releaseResources();
}

TEST_CASE("Kobber modulation dots keep moving in an open editor", "[kobber][processor][ui][lfo][modulation]")
{
	namespace parameters = vekt::mono::parameters;
	juce::ScopedJuceInitialiser_GUI initialiseJuce;
	vekt::mono::PluginProcessor processor;
	const auto set = [&](const char* identifier, float value)
	{
		auto* parameter = processor.getParameters().getParameter(identifier);
		parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
	};
	for (auto* parameter : processor.juce::AudioProcessor::getParameters())
		parameter->setValueNotifyingHost(parameter->getDefaultValue());
	set(parameters::filterCutoff, 1'000.0f);
	set(parameters::lfos[0].filter, 1.0f);
	set(parameters::lfos[0].rate, 1.0f);
	set(parameters::ampSustain, 100.0f);
	vekt::mono::PluginEditor editor(processor);
	auto* cutoff = findRotary(editor.getContent(), "Cutoff");
	REQUIRE(cutoff != nullptr);
	const auto& ring = cutoff->getModulationRing();
	REQUIRE_FALSE(ring.getModulation()->current.has_value());

	processor.prepareToPlay(48'000.0, 512);
	juce::AudioBuffer<float> buffer(2, 512);
	juce::MidiBuffer note;
	note.addEvent(juce::MidiMessage::noteOn(1, 57, 0.8f), 0);
	processor.processBlock(buffer, note);
	juce::MidiBuffer none;
	// A display frame per block, as a 94 Hz display would show 512-sample blocks at 48 kHz.
	auto now = 100.0;
	std::vector<double> positions;
	for (int frame = 0; frame < 4; ++frame)
	{
		for (int block = 0; block < 8; ++block)
		{
			processor.processBlock(buffer, none);
			editor.refreshModulationRings(now += blockSeconds);
		}
		REQUIRE(ring.getModulation()->current.has_value());
		REQUIRE(cutoffDotOutput(*ring.getModulation()->current, 1'000.0, 1.0)
			== Catch::Approx(processor.getLfoDisplayValue(0)).margin(displayedLfoTolerance));
		positions.push_back(*ring.getModulation()->current);
	}
	for (std::size_t frame = 1; frame < positions.size(); ++frame) REQUIRE(positions[frame] != Catch::Approx(positions[frame - 1]));

	// A depth change reaches the same editor's ring, and stopping hides the dot.
	set(parameters::lfos[0].filter, 2.0f);
	editor.refreshModulationRings(now += blockSeconds);
	REQUIRE(ring.getModulation()->highest == Catch::Approx(4'000.0));
	processor.releaseResources();
	editor.refreshModulationRings(now += blockSeconds);
	REQUIRE_FALSE(ring.getModulation()->current.has_value());
}

TEST_CASE("Kobber modulation dots follow the slow LFOs when a fast one shares the knob", "[kobber][processor][ui][lfo][modulation]")
{
	using vekt::mono::LfoDotContribution;
	using vekt::mono::LfoPolarity;
	const auto combine = [](std::initializer_list<LfoDotContribution> lfos)
	{
		const std::vector<LfoDotContribution> list(lfos);
		return vekt::mono::combineLfoDot(list);
	};
	REQUIRE(combine({}).opacity == Catch::Approx(0.0f));
	// A followable LFO moves the dot fully. Half-way through its handover it moves the dot half as far, and the band
	// covers the other half of its swing: the band grows as the dot fades, never leaving neither.
	auto dot = combine({ { 2.0f, 0.5f, LfoPolarity::bipolar, 1.0f } });
	REQUIRE(dot.offset == Catch::Approx(1.0f));
	REQUIRE(dot.blurHalfWidth == Catch::Approx(0.0f));
	dot = combine({ { 2.0f, 0.5f, LfoPolarity::bipolar, 0.5f } });
	REQUIRE(dot.offset == Catch::Approx(0.5f));
	REQUIRE(dot.opacity == Catch::Approx(0.5f));
	REQUIRE(dot.blurHalfWidth == Catch::Approx(1.0f));
	REQUIRE(combine({ { 2.0f, 0.5f, LfoPolarity::bipolar, 0.0f } }).opacity == Catch::Approx(0.0f));
	// Beside a slow LFO, a fast one sits at the centre of its swing: zero when bipolar, half its offset when unipolar.
	dot = combine({ { 1.0f, 0.8f, LfoPolarity::bipolar, 1.0f }, { 3.0f, -0.9f, LfoPolarity::bipolar, 0.0f } });
	REQUIRE(dot.offset == Catch::Approx(0.8f));
	REQUIRE(dot.opacity == Catch::Approx(1.0f));
	dot = combine({ { 1.0f, 0.8f, LfoPolarity::bipolar, 1.0f }, { 2.0f, 0.9f, LfoPolarity::unipolar, 0.0f } });
	REQUIRE(dot.offset == Catch::Approx(1.8f));
	// Part-way through their handovers each counts by its own share; the dot is as clear as the slower one.
	dot = combine({ { 1.0f, 0.8f, LfoPolarity::bipolar, 0.5f }, { 2.0f, 0.5f, LfoPolarity::bipolar, 0.25f } });
	REQUIRE(dot.offset == Catch::Approx(0.5f * 0.8f + 0.25f * 2.0f * 0.5f));
	REQUIRE(dot.blurHalfWidth == Catch::Approx(0.5f * 1.0f + 0.75f * 2.0f));
	REQUIRE(dot.opacity == Catch::Approx(0.5f));

	namespace parameters = vekt::mono::parameters;
	juce::ScopedJuceInitialiser_GUI initialiseJuce;
	vekt::mono::PluginProcessor processor;
	const auto set = [&](const char* identifier, float value)
	{
		auto* parameter = processor.getParameters().getParameter(identifier);
		parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
	};
	for (auto* parameter : processor.juce::AudioProcessor::getParameters())
		parameter->setValueNotifyingHost(parameter->getDefaultValue());
	// A slow sweep and a fast bipolar wobble on Cutoff.
	set(parameters::filterCutoff, 1'000.0f);
	set(parameters::lfos[0].filter, 1.0f);
	set(parameters::lfos[0].rate, 0.5f);
	set(parameters::lfos[1].filter, 2.0f);
	set(parameters::lfos[1].rate, 50.0f);
	set(parameters::ampSustain, 100.0f);
	// The same pair on a compact oscillator knob, for the snapshot.
	set(parameters::lfos[0].width[0], 40.0f);
	set(parameters::lfos[1].width[0], 20.0f);
	vekt::mono::PluginEditor editor(processor);
	processor.prepareToPlay(48'000.0, 512);
	juce::AudioBuffer<float> buffer(2, 512);
	juce::MidiBuffer note;
	note.addEvent(juce::MidiMessage::noteOn(1, 57, 0.8f), 0);
	processor.processBlock(buffer, note);
	juce::MidiBuffer none;
	auto now = 100.0;
	for (int block = 0; block < 20; ++block)
	{
		processor.processBlock(buffer, none);
		editor.refreshModulationRings(now += blockSeconds);
	}
	auto* cutoff = findRotary(editor.getContent(), "Cutoff");
	REQUIRE(cutoff != nullptr);
	const auto& modulation = *cutoff->getModulationRing().getModulation();
	// The arc still spans both; the dot is fully visible and shows only the slow sweep.
	REQUIRE(modulation.lowest == Catch::Approx(1'000.0 * std::exp2(-3.0)).epsilon(1.0e-4));
	REQUIRE(modulation.highest == Catch::Approx(1'000.0 * std::exp2(3.0)).epsilon(1.0e-4));
	REQUIRE(modulation.current.has_value());
	REQUIRE(modulation.currentOpacity == Catch::Approx(1.0f));
	REQUIRE(cutoffDotOutput(*modulation.current, 1'000.0, 1.0) == Catch::Approx(processor.getLfoDisplayValue(0)).margin(displayedLfoTolerance));
	// The fast wobble is a blur band of its two-octave swing either side of the dot, containing what is heard (up to
	// the few milliseconds the display shows the slow LFO behind).
	REQUIRE(modulation.blur.has_value());
	REQUIRE(modulation.blur->lowest == Catch::Approx(*modulation.current * std::exp2(-2.0)).epsilon(1.0e-4));
	REQUIRE(modulation.blur->highest == Catch::Approx(*modulation.current * std::exp2(2.0)).epsilon(1.0e-4));
	const auto heard = 1'000.0 * std::exp2(static_cast<double>(processor.getLfoDisplayValue(0)) + 2.0 * static_cast<double>(processor.getLfoDisplayValue(1)));
	REQUIRE(heard >= modulation.blur->lowest * std::exp2(-displayedLfoTolerance));
	REQUIRE(heard <= modulation.blur->highest * std::exp2(displayedLfoTolerance));
	if (const auto* path = std::getenv("VEKT_MONO_SNAPSHOT"))
	{
		editor.resized();
		writeSnapshot(editor, path);
	}

	// Alone, a fast LFO leaves no dot, only a band over its whole range: moving, unlike a depth with nothing playing.
	set(parameters::lfos[0].filter, 0.0f);
	editor.refreshModulationRings(now += blockSeconds);
	const auto& alone = *cutoff->getModulationRing().getModulation();
	REQUIRE_FALSE(alone.current.has_value());
	REQUIRE(alone.blur.has_value());
	REQUIRE(alone.blur->lowest == Catch::Approx(alone.lowest));
	REQUIRE(alone.blur->highest == Catch::Approx(alone.highest));
	// A slow LFO alone needs no band; silence shows neither.
	set(parameters::lfos[0].filter, 1.0f);
	set(parameters::lfos[1].filter, 0.0f);
	editor.refreshModulationRings(now += blockSeconds);
	REQUIRE_FALSE(cutoff->getModulationRing().getModulation()->blur.has_value());
	processor.releaseResources();
	set(parameters::lfos[1].filter, 2.0f);
	editor.refreshModulationRings(now += blockSeconds);
	REQUIRE_FALSE(cutoff->getModulationRing().getModulation()->blur.has_value());
	REQUIRE_FALSE(cutoff->getModulationRing().getModulation()->current.has_value());
}

TEST_CASE("Kobber modulation blur bands always contain the value heard", "[kobber][ui][lfo][modulation]")
{
	using vekt::mono::LfoDotContribution;
	using vekt::mono::LfoPolarity;
	const auto combine = [](std::initializer_list<LfoDotContribution> lfos)
	{
		const std::vector<LfoDotContribution> list(lfos);
		return vekt::mono::combineLfoDot(list);
	};
	// A followable LFO needs no band; a fast one beside a slow one adds its swing: all of it bipolar, half unipolar.
	REQUIRE(combine({ { 2.0f, 0.5f, LfoPolarity::bipolar, 1.0f } }).blurHalfWidth == Catch::Approx(0.0f));
	REQUIRE(combine({ { 1.0f, 0.8f, LfoPolarity::bipolar, 1.0f }, { -3.0f, 0.2f, LfoPolarity::bipolar, 0.0f } }).blurHalfWidth == Catch::Approx(3.0f));
	REQUIRE(combine({ { 1.0f, 0.8f, LfoPolarity::bipolar, 1.0f }, { 2.0f, 0.2f, LfoPolarity::unipolar, 0.0f } }).blurHalfWidth == Catch::Approx(1.0f));
	// All too fast: no dot, and a band over the whole reach (here 0..2 for a unipolar +2).
	const auto fast = combine({ { 2.0f, 0.9f, LfoPolarity::unipolar, 0.0f } });
	REQUIRE(fast.opacity == Catch::Approx(0.0f));
	REQUIRE(fast.offset - fast.blurHalfWidth == Catch::Approx(0.0f));
	REQUIRE(fast.offset + fast.blurHalfWidth == Catch::Approx(2.0f));

	// Whatever the outputs, rates and polarities, the heard offset lies within the band.
	juce::Random random(7);
	for (int trial = 0; trial < 2'000; ++trial)
	{
		std::array<LfoDotContribution, 2> lfos {};
		auto heard = 0.0f;
		for (auto& lfo : lfos)
		{
			lfo.polarity = random.nextBool() ? LfoPolarity::unipolar : LfoPolarity::bipolar;
			lfo.offset = random.nextFloat() * 8.0f - 4.0f;
			lfo.output = lfo.polarity == LfoPolarity::unipolar ? random.nextFloat() : random.nextFloat() * 2.0f - 1.0f;
			lfo.opacity = random.nextBool() ? 1.0f : random.nextFloat();
			heard += lfo.offset * lfo.output;
		}
		const auto dot = vekt::mono::combineLfoDot(lfos);
		INFO("Trial " << trial);
		REQUIRE(std::abs(heard - dot.offset) <= dot.blurHalfWidth + 1.0e-5f);
	}
}

namespace
{
juce::Label* findNamedLabel(juce::Component& parent, const juce::String& name)
{
	for (auto* child : parent.getChildren())
	{
		if (auto* label = dynamic_cast<juce::Label*>(child); label != nullptr && label->getName() == name) return label;
		if (auto* label = findNamedLabel(*child, name)) return label;
	}
	return nullptr;
}

// No two visible siblings overlap, here and in every Panel below (a control's own parts may layer).
void checkNoOverlaps(juce::Component& parent)
{
	std::vector<juce::Component*> visible;
	for (auto* child : parent.getChildren())
		if (child->isVisible()) visible.push_back(child);
	for (std::size_t first = 0; first < visible.size(); ++first)
		for (auto second = first + 1; second < visible.size(); ++second)
		{
			INFO(visible[first]->getName().toStdString() << " and " << visible[second]->getName().toStdString());
			REQUIRE_FALSE(visible[first]->getBounds().intersects(visible[second]->getBounds()));
		}
	for (auto* child : visible)
		if (dynamic_cast<vekt::ui::Panel*>(child) != nullptr) checkNoOverlaps(*child);
}

// Where a control sits in the editor's logical canvas.
juce::Rectangle<int> canvasBounds(juce::Component& content, juce::Component& component)
{
	return content.getLocalArea(component.getParentComponent(), component.getBounds());
}

bool visibleRotaryNamed(juce::Component& content, const char* name)
{
	auto* rotary = findRotary(content, name);
	return rotary != nullptr && rotary->isVisible();
}

void setParameter(juce::AudioProcessorValueTreeState& state, const char* identifier, float value)
{
	auto* parameter = state.getParameter(identifier);
	parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}
}

TEST_CASE("Flint editor keeps the shared controls in place and rebinds the model's own", "[flint][processor][ui]")
{
	using vekt::flint::Mode;
	juce::ScopedJuceInitialiser_GUI initialiseJuce;
	vekt::flint::PluginProcessor processor;
	vekt::flint::PluginEditor editor(processor);
	auto& content = editor.getContent();
	auto& state = processor.getParameters();
	REQUIRE(editor.getWidth() == 1120);
	REQUIRE(editor.getHeight() == 700);
	checkVisibleBounds(content);
	checkNoOverlaps(content);
	checkOutputScope(content, 160, 100);
	REQUIRE(findMeter(content, "OUT") != nullptr);

	// Every mode is offered; only those with a model in this build can be chosen (A21: unavailable entries marked).
	for (std::size_t index = 0; index < vekt::flint::modeCount; ++index)
	{
		const juce::String name = vekt::flint::modes[index].name;
		auto* button = findNamedButton(content, name + " mode");
		INFO(name.toStdString());
		REQUIRE(button != nullptr);
		REQUIRE(button->isEnabled() == (name == "Kick" || name == "Mallet"));
	}
	REQUIRE(findNamedButton(content, "Kick mode")->getToggleState());
	REQUIRE(visibleRotaryNamed(content, "Velocity"));
	REQUIRE(visibleRotaryNamed(content, "Variation"));
	REQUIRE(findNamedButton(content, "Classic Analog model")->getToggleState());
	REQUIRE_FALSE(findNamedButton(content, "Punch Analog model")->isEnabled());

	constexpr std::array sharedNames { "Pitch", "Attack", "Decay", "Tone", "Drive" };
	const auto sharedPlaces = [&]
	{
		std::vector<juce::Rectangle<int>> places;
		for (const auto* name : sharedNames) places.push_back(canvasBounds(content, *findRotary(content, name)));
		places.push_back(canvasBounds(content, *findNamedLabel(content, "Pitch value")));
		return places;
	};
	const auto kickPlaces = sharedPlaces();
	const auto visibleRotary = [&](const char* name)
	{
		auto* rotary = findRotary(content, name);
		return rotary != nullptr && rotary->isVisible();
	};
	for (const auto* name : { "Sweep", "Sweep Time", "Click", "Body Shape" }) REQUIRE(visibleRotary(name));
	REQUIRE_FALSE(visibleRotary("Material"));
	REQUIRE(findNamedLabel(content, "Pitch value")->getText() == juce::String::fromUTF8("A1 \xc2\xb7 55.0 Hz"));
	REQUIRE(findNamedLabel(content, "Decay detail")->getText() == "T60 333 ms");

	// Drive Type sits under Drive.
	const auto drive = canvasBounds(content, *findRotary(content, "Drive"));
	for (const auto* name : { "Soft drive", "Hard drive", "Fold drive" })
	{
		const auto button = canvasBounds(content, *findNamedButton(content, name));
		REQUIRE(button.getY() >= drive.getBottom());
		REQUIRE(button.getX() >= drive.getX());
		REQUIRE(button.getRight() <= drive.getRight());
	}
	findNamedButton(content, "Fold drive")->setToggleState(true, juce::sendNotificationSync);
	REQUIRE(juce::roundToInt(state.getRawParameterValue(vekt::flint::parameters::driveType)->load()) == 2);

	// A Mode change sets the mode's start values and rebinds the model slots; the shared controls stay where they are.
	findNamedButton(content, "Mallet mode")->onClick();
	REQUIRE(juce::roundToInt(state.getRawParameterValue(vekt::flint::parameters::mode)->load())
	        == static_cast<int>(Mode::mallet));
	REQUIRE(state.getRawParameterValue(vekt::flint::parameters::pitch)->load() == Catch::Approx(60.0f));
	REQUIRE(editor.shownModel() == vekt::flint::ModelId::malletBar);
	for (const auto* name : { "Material", "Hardness", "Position", "Overtones", "Resonator" }) REQUIRE(visibleRotary(name));
	REQUIRE_FALSE(visibleRotary("Sweep"));
	REQUIRE(findNamedLabel(content, "Overtones detail")->getText() == "Marimba");
	// A rebound slot drives its new parameter, and its readout follows at once (no timer tick).
	const auto sweepBefore = state.getRawParameterValue(vekt::flint::parameters::kickSweep)->load();
	findRotary(content, "Hardness")->getSlider().setValue(0.0, juce::sendNotificationSync);
	REQUIRE(state.getRawParameterValue(vekt::flint::parameters::barHardness)->load() == Catch::Approx(0.0f));
	REQUIRE(state.getRawParameterValue(vekt::flint::parameters::kickSweep)->load() == Catch::Approx(sweepBefore));
	REQUIRE(findNamedLabel(content, "Hardness detail")->getText() == "contact 6.00 ms");
	findRotary(content, "Hardness")->getSlider().setValue(50.0, juce::sendNotificationSync);
	REQUIRE(sharedPlaces() == kickPlaces);
	checkVisibleBounds(content);
	checkNoOverlaps(content);
	// The editor follows undo and redo of the Mode change (one step, A31).
	processor.getUndoManager().undo();
	REQUIRE(editor.shownModel() == vekt::flint::ModelId::kickClassicAnalog);
	REQUIRE(state.getRawParameterValue(vekt::flint::parameters::pitch)->load() == Catch::Approx(33.0f));
	processor.getUndoManager().redo();
	REQUIRE(editor.shownModel() == vekt::flint::ModelId::malletBar);

	// Pitch below the Bar's range: the readout shows the note played, the arc beyond the range is dimmed.
	auto& pitch = findRotary(content, "Pitch")->getSlider();
	REQUIRE(static_cast<double>(pitch.getProperties()["playableFrom"]) == Catch::Approx(36.0));
	REQUIRE(static_cast<double>(pitch.getProperties()["playableTo"]) == Catch::Approx(108.0));
	setParameter(state, vekt::flint::parameters::pitch, 30.0f);
	REQUIRE(findNamedLabel(content, "Pitch value")->getText() == juce::String::fromUTF8("C2 \xc2\xb7 65.4 Hz"));
	REQUIRE(findNamedLabel(content, "Pitch detail")->getText() == "below range: F#1");
	setParameter(state, vekt::flint::parameters::pitch, 60.0f);
	REQUIRE(findNamedLabel(content, "Pitch detail")->getText().isEmpty());
	setParameter(state, vekt::flint::parameters::decay, 0.0f);
	REQUIRE(findNamedLabel(content, "Decay detail")->getText() == "T60 " + juce::String(juce::roundToInt(1'000.0 * 0.05)) + " ms");
	setParameter(state, vekt::flint::parameters::pitch, 30.0f);

	// Pitch drags snap to semitones; text entry takes notes, cents and frequencies.
	REQUIRE(pitch.snapValue(45.3, juce::Slider::absoluteDrag) == Catch::Approx(45.0));
	REQUIRE(pitch.snapValue(45.3, juce::Slider::notDragging) == Catch::Approx(45.3));
	REQUIRE(pitch.getValueFromText("D#2 +12 ct") == Catch::Approx(39.12));
	REQUIRE(pitch.getValueFromText("440 Hz") == Catch::Approx(69.0));
	REQUIRE(pitch.getValueFromText("1.2 kHz") == Catch::Approx(69.0 + 12.0 * std::log2(1'200.0 / 440.0)));
	REQUIRE(pitch.getValueFromText("H9") == Catch::Approx(pitch.getValue()));
	REQUIRE(static_cast<bool>(pitch.getProperties()["valueEntryError"]));

	findNamedButton(content, "Kick mode")->onClick();
	REQUIRE(editor.shownModel() == vekt::flint::ModelId::kickClassicAnalog);

	// Settings: quality, Note Off Damps and New Seed in the anchored pop-over; the active quality in the I/O strip.
	vekt::ui::QualitySettings* settings = nullptr;
	for (auto* child : content.getChildren())
		if (auto* candidate = dynamic_cast<vekt::ui::QualitySettings*>(child)) settings = candidate;
	REQUIRE(settings != nullptr);
	settings->setOpen(true);
	checkVisibleBounds(content);
	REQUIRE(settings->isVisible());
	REQUIRE(settings->getTrackingBox().isVisible());
	REQUIRE(settings->getOfflineBox().isVisible());
	REQUIRE(findNamedButton(*settings, "Note Off Damps") != nullptr);
	auto* newSeed = findNamedButton(*settings, "New Seed");
	REQUIRE(newSeed != nullptr);
	checkNoOverlaps(*settings);
	const auto seed = processor.getSeed();
	newSeed->onClick();
	REQUIRE(processor.getSeed() != seed);
	settings->setOpen(false);
	REQUIRE(findNamedLabel(content, "Active quality")->getText() == "Quality: Off");

	editor.setSize(2240, 1400);
	checkVisibleBounds(content);
	REQUIRE(sharedPlaces() == kickPlaces);

	// Opt-in render artefacts for visual review (A21); normal test runs do not write files.
	editor.setSize(1120, 700);
	if (const auto* path = std::getenv("VEKT_FLINT_SNAPSHOT")) writeSnapshot(editor, path);
	if (const auto* path = std::getenv("VEKT_FLINT_SNAPSHOT_MALLET"))
	{
		processor.selectMode(Mode::mallet);
		writeSnapshot(editor, path);
	}
	if (const auto* path = std::getenv("VEKT_FLINT_SNAPSHOT_SETTINGS"))
	{
		settings->setOpen(true);
		writeSnapshot(editor, path);
	}
}
