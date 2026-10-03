#include <vekt/ui/QualitySettings.h>

#include <vekt/ui/ChoiceItems.h>

namespace vekt::ui
{
QualitySettings::QualitySettings(juce::AudioProcessorValueTreeState& parameters, const char* trackingIdentifier,
	const char* offlineIdentifier)
	: Panel("Quality settings")
{
	trackingLabel.setText("Tracking", juce::dontSendNotification);
	offlineLabel.setText("Offline", juce::dontSendNotification);
	trackingBox.setName("Tracking quality");
	offlineBox.setName("Offline quality");
	trackingBox.setTooltip("Oversampling during real-time playback");
	offlineBox.setTooltip("Oversampling when the host renders offline (bounce or export)");
	addChoiceItems(trackingBox, parameters, trackingIdentifier);
	addChoiceItems(offlineBox, parameters, offlineIdentifier);
	for (auto* component : { static_cast<juce::Component*>(&trackingLabel), static_cast<juce::Component*>(&trackingBox),
			 static_cast<juce::Component*>(&offlineLabel), static_cast<juce::Component*>(&offlineBox),
			 static_cast<juce::Component*>(&closeButton) })
		addAndMakeVisible(*component);
	trackingAttachment = std::make_unique<ComboBoxAttachment>(parameters, trackingIdentifier, trackingBox);
	offlineAttachment = std::make_unique<ComboBoxAttachment>(parameters, offlineIdentifier, offlineBox);
	settingsButton.setClickingTogglesState(true);
	settingsButton.onClick = [this] { setOpen(settingsButton.getToggleState()); };
	closeButton.onClick = [this]
	{
		setOpen(false);
		if (settingsButton.isShowing())
			settingsButton.grabKeyboardFocus();
	};
	setVisible(false);
}

QualitySettings::~QualitySettings() = default;

void QualitySettings::setOpen(bool open)
{
	setVisible(open);
	settingsButton.setToggleState(open, juce::dontSendNotification);
	if (!open)
		return;
	toFront(false);
	if (trackingBox.isShowing())
		trackingBox.grabKeyboardFocus();
}

void QualitySettings::resized()
{
	const auto content = getContentBounds();
	trackingLabel.setBounds(content.withHeight(24));
	trackingBox.setBounds(content.withTrimmedTop(24).withHeight(36));
	offlineLabel.setBounds(content.withTrimmedTop(68).withHeight(24));
	offlineBox.setBounds(content.withTrimmedTop(92).withHeight(36));
	closeButton.setBounds(getWidth() - 84, 4, 72, 28);
}
}
