#include <vekt/ui/ScalableEditor.h>

namespace vekt::ui
{
ScalableEditor::ScalableEditor(juce::AudioProcessor& audioProcessor)
	: AudioProcessorEditor(audioProcessor)
{
	constrainer.setMinimumSize(720, 540);
	constrainer.setMaximumSize(1600, 1100);
	constrainer.setFixedAspectRatio(static_cast<double>(logicalWidth) / logicalHeight);
	setResizable(true, true);
    setConstrainer(&constrainer);
    addAndMakeVisible(content);
	addAndMakeVisible(resizeHandle);
    setSize(logicalWidth, logicalHeight);
    resized();
}

juce::Component& ScalableEditor::getContent() noexcept { return content; }

void ScalableEditor::setResizeHandleVisible(bool shouldBeVisible) noexcept
{
	resizeHandle.setVisible(shouldBeVisible);
}

void ScalableEditor::resized()
{
	const auto scale = static_cast<float>(getWidth()) / logicalWidth;
	content.setBounds(0, 0, logicalWidth, logicalHeight);
	content.setTransform(juce::AffineTransform::scale(scale));
	resizeHandle.setBounds(getLocalBounds().removeFromRight(18).removeFromBottom(18));
}
}
