#pragma once

#include <vekt/flint/PluginProcessor.h>

#include <vekt/preset_ui/PresetBrowser.h>
#include <vekt/ui/LevelMeter.h>
#include <vekt/ui/ModeButton.h>
#include <vekt/ui/Oscilloscope.h>
#include <vekt/ui/Panel.h>
#include <vekt/ui/PresetNavigation.h>
#include <vekt/ui/QualitySettings.h>
#include <vekt/ui/RotaryControl.h>
#include <vekt/ui/ScalableEditor.h>
#include <vekt/ui/UndoRedoControls.h>
#include <vekt/ui/VektLookAndFeel.h>

#include <array>
#include <memory>
#include <optional>

namespace vekt::flint
{
// Flint's editor (docs/FLINT_VALIDATION.md, Parameters, and docs/UI_UX.md): the Mode row, the Model row, the shared
// controls in fixed places, the selected model's controls in five slots that rebind, Velocity and Variation as a pair,
// the I/O strip with the Level fader, and Settings with quality, Note Off Damps and New Seed.
class PluginEditor final : public ui::ScalableEditor, private juce::Timer, private juce::Slider::Listener
{
public:
	explicit PluginEditor(PluginProcessor& processor);
	~PluginEditor() override;
	void paint(juce::Graphics&) override;
	void resized() override;

	// The model the editor shows, nothing while the mode has no model of its own yet (tests).
	[[nodiscard]] std::optional<ModelId> shownModel() const noexcept { return boundModel; }

private:
	using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
	using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

	void timerCallback() override;
	// Every shared and model control refreshes the unit lines at once, so they never trail a change.
	void sliderValueChanged(juce::Slider*) override { refreshDetails(); }
	void refreshPresetLabel();
	void refreshSelection();
	void bindModelControls(std::optional<ModelId> model);
	void refreshDetails();
	void configureRotary(juce::Component& parent, ui::RotaryControl& control, const char* name, const char* parameter,
	    std::unique_ptr<SliderAttachment>& attachment);
	[[nodiscard]] Mode selectedMode() const noexcept;
	[[nodiscard]] std::optional<ModelId> selectedModel() const noexcept;
	[[nodiscard]] float parameterValue(const char* identifier) const noexcept;
	[[nodiscard]] juce::String pitchDisplay(double note) const;

	PluginProcessor& pluginProcessor;
	ui::VektLookAndFeel lookAndFeel;
	juce::Label title;
	ui::UndoRedoControls historyControls;
	ui::PresetNavigation presetNavigation;
	preset_ui::PresetBrowser presetBrowser;
	ui::QualitySettings settings;
	juce::ToggleButton noteOffDampsButton { "Note Off Damps" };
	juce::TextButton newSeedButton { "New Seed" };

	std::array<ui::ModeButton, modeCount> modeButtons;
	std::array<ui::ModeButton, 5> modelButtons;

	ui::Panel soundPanel { "Sound" };
	ui::Panel modelPanel { "Model" };
	ui::Panel feelPanel { "Velocity & Variation" };
	ui::Panel ioPanel { "I/O" };

	// Pitch, Attack, Decay, Tone, Drive: fixed for every mode and model, each with its unit in the model beneath.
	std::array<ui::RotaryControl, 5> sharedControls;
	std::array<std::unique_ptr<SliderAttachment>, 5> sharedAttachments;
	std::array<juce::Label, 5> sharedDetails;
	std::array<juce::TextButton, 3> driveTypeButtons;
	std::array<ui::RotaryControl, parameters::maximumModelControls> modelControls;
	std::array<std::unique_ptr<SliderAttachment>, parameters::maximumModelControls> modelAttachments;
	std::array<juce::Label, parameters::maximumModelControls> modelDetails;
	std::array<ui::RotaryControl, 2> feelControls;
	std::array<std::unique_ptr<SliderAttachment>, 2> feelAttachments;

	juce::Label qualityLabel;
	juce::Slider levelFader;
	ui::LevelMeter outputMeter { "OUT", juce::Colour::fromRGB(227, 156, 75), ui::LevelMeter::Orientation::vertical };
	ui::Oscilloscope outputScope { pluginProcessor.getOutputScope() };

	std::unique_ptr<juce::ParameterAttachment> modeAttachment;
	std::unique_ptr<juce::ParameterAttachment> kickModelAttachment;
	std::unique_ptr<juce::ParameterAttachment> malletModelAttachment;
	std::unique_ptr<juce::ParameterAttachment> driveTypeAttachment;
	std::unique_ptr<ButtonAttachment> noteOffDampsAttachment;
	std::unique_ptr<SliderAttachment> levelAttachment;
	std::optional<ModelId> boundModel;
	bool modelBound {};
};
}
