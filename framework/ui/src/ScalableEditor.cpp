#include <vekt/ui/ScalableEditor.h>

namespace vekt::ui
{
ScalableEditor::ScalableEditor(juce::AudioProcessor& audioProcessor)
	: AudioProcessorEditor(audioProcessor)
{
	constrainer.setMinimumSize(540, 360);
	constrainer.setMaximumSize(1440, 960);
	constrainer.setFixedAspectRatio(static_cast<double>(logicalWidth) / logicalHeight);
	setResizable(true, true);
	setConstrainer(&constrainer);
	setSize(logicalWidth, logicalHeight);
	addAndMakeVisible(content);
	addAndMakeVisible(resizeHandle);
}

juce::Component& ScalableEditor::getContent() noexcept { return content; }

void ScalableEditor::resized()
{
	const auto scale = static_cast<float>(getWidth()) / logicalWidth;
	content.setBounds(0, 0, logicalWidth, logicalHeight);
	content.setTransform(juce::AffineTransform::scale(scale));
	resizeHandle.setBounds(getLocalBounds().removeFromRight(18).removeFromBottom(18));
}
}
