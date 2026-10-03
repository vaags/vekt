#include <vekt/dsp/OversamplingChoices.h>
#include <vekt/plugin_support/QualitySelection.h>
#include <vekt/ui/QualitySettings.h>

#include <catch2/catch_test_macros.hpp>

namespace
{
// The smallest processor that owns the two shared quality parameters.
class QualityProcessor final : public juce::AudioProcessor
{
public:
	QualityProcessor() : parameters(*this, nullptr, "QualityTest", layout()) {}
	const juce::String getName() const override { return "Quality"; }
	void prepareToPlay(double, int) override {}
	void releaseResources() override {}
	void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
	double getTailLengthSeconds() const override { return 0.0; }
	bool acceptsMidi() const override { return false; }
	bool producesMidi() const override { return false; }
	juce::AudioProcessorEditor* createEditor() override { return nullptr; }
	bool hasEditor() const override { return false; }
	int getNumPrograms() override { return 1; }
	int getCurrentProgram() override { return 0; }
	void setCurrentProgram(int) override {}
	const juce::String getProgramName(int) override { return {}; }
	void changeProgramName(int, const juce::String&) override {}
	void getStateInformation(juce::MemoryBlock&) override {}
	void setStateInformation(const void*, int) override {}

	juce::AudioProcessorValueTreeState parameters;

private:
	static juce::AudioProcessorValueTreeState::ParameterLayout layout()
	{
		return { vekt::plugin_support::QualitySelection::makeTrackingParameter("tracking", 1, 2),
			vekt::plugin_support::QualitySelection::makeOfflineParameter("offline", 1, 4) };
	}
};
}

TEST_CASE("Quality settings offer the shared choices bound to the product's parameters", "[ui][quality]")
{
	juce::ScopedJuceInitialiser_GUI initialiseJuce;
	QualityProcessor processor;
	vekt::ui::QualitySettings settings(processor.parameters, "tracking", "offline");
	settings.setSize(vekt::ui::QualitySettings::preferredWidth, vekt::ui::QualitySettings::preferredHeight);
	auto& tracking = settings.getTrackingBox();
	auto& offline = settings.getOfflineBox();
	REQUIRE(tracking.getName() == "Tracking quality");
	REQUIRE(offline.getName() == "Offline quality");
	REQUIRE(tracking.getNumItems() == vekt::dsp::trackingQualityChoices().size());
	REQUIRE(offline.getNumItems() == vekt::dsp::offlineQualityChoices().size());
	for (int index = 0; index < tracking.getNumItems(); ++index)
		REQUIRE(tracking.getItemText(index) == vekt::dsp::trackingQualityChoices()[index]);
	for (int index = 0; index < offline.getNumItems(); ++index)
		REQUIRE(offline.getItemText(index) == vekt::dsp::offlineQualityChoices()[index]);
	REQUIRE(tracking.getSelectedItemIndex() == 2);
	REQUIRE(offline.getSelectedItemIndex() == 4);
	tracking.setSelectedItemIndex(6, juce::sendNotificationSync);
	offline.setSelectedItemIndex(1, juce::sendNotificationSync);
	REQUIRE(dynamic_cast<juce::AudioParameterChoice*>(processor.parameters.getParameter("tracking"))->getIndex() == 6);
	REQUIRE(dynamic_cast<juce::AudioParameterChoice*>(processor.parameters.getParameter("offline"))->getIndex() == 1);
	// Every child sits inside the panel.
	for (auto* child : settings.getChildren())
		REQUIRE(settings.getLocalBounds().contains(child->getBounds()));
}

TEST_CASE("Quality settings open and close with the Settings toggle and Close", "[ui][quality]")
{
	juce::ScopedJuceInitialiser_GUI initialiseJuce;
	QualityProcessor processor;
	vekt::ui::QualitySettings settings(processor.parameters, "tracking", "offline");
	auto& toggle = settings.getSettingsButton();
	REQUIRE(toggle.getButtonText() == "Settings");
	REQUIRE_FALSE(settings.isVisible());
	toggle.setToggleState(true, juce::dontSendNotification);
	toggle.onClick();
	REQUIRE(settings.isVisible());
	juce::Button* close = nullptr;
	for (auto* child : settings.getChildren())
		if (auto* button = dynamic_cast<juce::Button*>(child); button != nullptr && button->getButtonText() == "Close") close = button;
	REQUIRE(close != nullptr);
	close->onClick();
	REQUIRE_FALSE(settings.isVisible());
	REQUIRE_FALSE(toggle.getToggleState());
	settings.setOpen(true);
	REQUIRE(toggle.getToggleState());
	toggle.setToggleState(false, juce::dontSendNotification);
	toggle.onClick();
	REQUIRE_FALSE(settings.isVisible());
}
