#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace vekt::ui
{
class ScalableEditor : public juce::AudioProcessorEditor
{
public:
	explicit ScalableEditor(juce::AudioProcessor& processor);

	[[nodiscard]] juce::Component& getContent() noexcept;
	void setResizeHandleVisible(bool shouldBeVisible) noexcept;
	void resized() override;

	inline static constexpr auto logicalWidth = 1040;
	inline static constexpr auto logicalHeight = 650;

private:
	juce::Component content;
	juce::ComponentBoundsConstrainer constrainer;
	juce::ResizableCornerComponent resizeHandle { this, &constrainer };
};
}
