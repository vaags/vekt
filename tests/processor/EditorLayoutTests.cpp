#include <PluginEditor.h>
#include <Parameters.h>
#include "../../plugins/vekt_glimmer/Source/PluginEditor.h"

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
	REQUIRE(editor.getWidth() == 1040);
	REQUIRE(editor.getHeight() == 650);
	REQUIRE(editor.getConstrainer()->getFixedAspectRatio() == 1.6);

	for (const auto width : { 1040, 1560, 2080 })
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

	for (const auto width : { 1040, 1560, 2080 })
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
		editor.setSize(1040, 650);
		const auto image = editor.createComponentSnapshot(editor.getLocalBounds(), true, 2.0f);
		juce::FileOutputStream stream { juce::File(juce::String(path)) };
		REQUIRE(stream.openedOk());
		REQUIRE(stream.setPosition(0));
		REQUIRE(stream.truncate().wasOk());
		REQUIRE(juce::PNGImageFormat().writeImageToStream(image, stream));
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
	REQUIRE(classic.getY() == 80);
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
	for (const auto width : { 1040, 1560, 2080 })
	{
		editor.setSize(width, width * 10 / 16);
		checkVisibleBounds(editor.getContent());
	}
	if (const auto* path = std::getenv("VEKT_GLIMMER_BROWSER_SNAPSHOT"))
	{
		editor.setSize(1040, 650);
		const auto image = editor.createComponentSnapshot(editor.getLocalBounds(), true, 2.0f);
		juce::FileOutputStream stream { juce::File(juce::String(path)) };
		REQUIRE(stream.openedOk());
		REQUIRE(stream.setPosition(0));
		REQUIRE(stream.truncate().wasOk());
		REQUIRE(juce::PNGImageFormat().writeImageToStream(image, stream));
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
