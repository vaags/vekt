#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace vekt::ui
{
class ScalableEditor : public juce::AudioProcessorEditor
{
public:
	// Products may choose their own logical size; the default is shared by the others.
	explicit ScalableEditor(juce::AudioProcessor& processor, int width = logicalWidth, int height = logicalHeight);

	[[nodiscard]] juce::Component& getContent() noexcept;
	[[nodiscard]] int getLogicalWidth() const noexcept { return contentWidth; }
	[[nodiscard]] int getLogicalHeight() const noexcept { return contentHeight; }
	void setResizeHandleVisible(bool shouldBeVisible) noexcept;
	void resized() override;

	inline static constexpr auto logicalWidth = 1120;
	inline static constexpr auto logicalHeight = 700;

private:
	int contentWidth, contentHeight;
	juce::Component content;
	juce::ComponentBoundsConstrainer constrainer;
	juce::ResizableCornerComponent resizeHandle { this, &constrainer };
};
}
