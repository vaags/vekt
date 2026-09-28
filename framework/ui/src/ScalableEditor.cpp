#include <vekt/ui/ScalableEditor.h>

namespace vekt::ui
{
ScalableEditor::ScalableEditor(juce::AudioProcessor& audioProcessor, int width, int height)
	: AudioProcessorEditor(audioProcessor), contentWidth(width), contentHeight(height)
{
	constrainer.setMinimumSize(contentWidth, contentHeight);
	constrainer.setMaximumSize(contentWidth * 2, contentHeight * 2);
	constrainer.setFixedAspectRatio(static_cast<double>(contentWidth) / contentHeight);
	setResizable(true, true);
    setConstrainer(&constrainer);
    addAndMakeVisible(content);
	addAndMakeVisible(resizeHandle);
    setSize(contentWidth, contentHeight);
    resized();
}

juce::Component& ScalableEditor::getContent() noexcept { return content; }

void ScalableEditor::setResizeHandleVisible(bool shouldBeVisible) noexcept
{
	resizeHandle.setVisible(shouldBeVisible);
}

void ScalableEditor::resized()
{
	const auto scale = static_cast<float>(getWidth()) / static_cast<float>(contentWidth);
	content.setBounds(0, 0, contentWidth, contentHeight);
	content.setTransform(juce::AffineTransform::scale(scale));
	resizeHandle.setBounds(getLocalBounds().removeFromRight(44).removeFromBottom(44));
}
}
