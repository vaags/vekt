#include <PluginEditor.h>

#include <catch2/catch_test_macros.hpp>

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

	for (const auto scale : { 1, 2 })
	{
		editor.setSize(1040 * scale, 650 * scale);
		checkVisibleBounds(editor.getContent());
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
}