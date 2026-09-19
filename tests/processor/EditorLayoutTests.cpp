#include <PluginEditor.h>
#include <Parameters.h>

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>

namespace
{
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
}

TEST_CASE("Rav editor keeps its controls within the 16:10 canvas", "[processor][ui]")
{
	juce::ScopedJuceInitialiser_GUI initialiseJuce;
	vekt::rav::PluginProcessor processor;
	vekt::rav::PluginEditor editor(processor);
	REQUIRE(editor.getWidth() == 1040);
	REQUIRE(editor.getHeight() == 650);
	REQUIRE(editor.getConstrainer()->getFixedAspectRatio() == 1.6);

	for (const auto width : { 1040, 1560, 2080 })
	{
		editor.setSize(width, width * 10 / 16);
		checkVisibleBounds(editor.getContent());
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
		editor.setSize(1040, 650);
		const auto image = editor.createComponentSnapshot(editor.getLocalBounds(), true, 2.0f);
		juce::FileOutputStream stream { juce::File(juce::String(path)) };
		REQUIRE(stream.openedOk());
		REQUIRE(juce::PNGImageFormat().writeImageToStream(image, stream));
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
	for (const auto width : { 1040, 1560, 2080 })
	{
		editor.setSize(width, width * 10 / 16);
		checkVisibleBounds(editor.getContent());
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
