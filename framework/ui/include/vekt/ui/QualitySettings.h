#pragma once

#include <vekt/ui/Panel.h>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace vekt::ui
{
// The Tracking and Offline oversampling choices (dsp/OversamplingChoices.h) in an anchored pop-over, opened and closed
// by its Settings toggle (UI_UX.md). The editor places the toggle in its toolbar, adds this panel as a hidden child
// and sets its bounds (preferredWidth x preferredHeight); the boxes bind to the product's two quality parameters.
// Opening focuses the Tracking box; Close returns focus to the toggle.
class QualitySettings final : public Panel
{
public:
	QualitySettings(juce::AudioProcessorValueTreeState& parameters, const char* trackingIdentifier,
		const char* offlineIdentifier);
	~QualitySettings() override;

	[[nodiscard]] juce::TextButton& getSettingsButton() noexcept { return settingsButton; }
	[[nodiscard]] juce::ComboBox& getTrackingBox() noexcept { return trackingBox; }
	[[nodiscard]] juce::ComboBox& getOfflineBox() noexcept { return offlineBox; }
	void setOpen(bool open);
	void resized() override;

	static constexpr int preferredWidth = 380;
	static constexpr int preferredHeight = 232;

private:
	using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

	juce::TextButton settingsButton { "Settings" };
	juce::TextButton closeButton { "Close" };
	juce::Label trackingLabel, offlineLabel;
	juce::ComboBox trackingBox, offlineBox;
	std::unique_ptr<ComboBoxAttachment> trackingAttachment, offlineAttachment;
};
}
