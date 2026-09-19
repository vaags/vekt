#include <vekt/ui/RotaryControl.h>

namespace vekt::ui
{
namespace
{
constexpr int labelHeight = 24;
constexpr int valueHeight = 22;

juce::Font valueFont()
{
	auto font = juce::Font(juce::FontOptions(14.0f));
	font.setTypefaceName(juce::Font::getDefaultMonospacedFontName());
	return font;
}
}

RotaryControl::RotaryControl()
{
	slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
	slider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
	slider.onValueChange = [this] { updateValueText(); };
	valueLabel.setFont(valueFont());
	valueLabel.setJustificationType(juce::Justification::centred);
	valueLabel.setBorderSize({});
	valueLabel.setColour(juce::Label::backgroundColourId, juce::Colour::fromRGB(23, 29, 32));
	valueLabel.setColour(juce::Label::outlineColourId, juce::Colour::fromRGB(116, 128, 132));
	valueLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(222, 224, 219));
	valueLabel.setColour(juce::Label::backgroundWhenEditingColourId, juce::Colour::fromRGB(29, 38, 42));
	valueLabel.setColour(juce::Label::outlineWhenEditingColourId, juce::Colour::fromRGB(227, 156, 75));
	valueLabel.setColour(juce::Label::textWhenEditingColourId, juce::Colour::fromRGB(242, 239, 225));
	valueLabel.setEditable(true, true, false);
	valueLabel.onEditorShow = [this]
	{
		valueLabel.setTooltip({});
		if (auto* editor = valueLabel.getCurrentTextEditor())
		{
			editor->setFont(valueFont());
			editor->setJustification(juce::Justification::centred);
			editor->setIndents(0, 0);
			editor->setBorder({});
		}
	};
	valueLabel.onTextChange = [this]
	{
		const auto text = valueLabel.getText();
		const auto value = slider.getValueFromText(text);
		if (static_cast<bool>(slider.getProperties()["valueEntryError"]))
		{
			valueLabel.setColour(juce::Label::outlineColourId, juce::Colour::fromRGB(224, 113, 90));
			valueLabel.setTooltip("Invalid value. Enter a number with the displayed unit.");
			return;
		}
		valueLabel.setColour(juce::Label::outlineColourId, juce::Colour::fromRGB(116, 128, 132));
		valueLabel.setTooltip({});
		if (!juce::approximatelyEqual(value, slider.getValue()))
		{
			juce::Slider::ScopedDragNotification gesture(slider);
			slider.setValue(value, juce::sendNotificationSync);
		}
		updateValueText();
	};
	label.setFont(juce::FontOptions(14.0f));
	label.setJustificationType(juce::Justification::centred);
	addAndMakeVisible(slider);
	addAndMakeVisible(valueLabel);
	addAndMakeVisible(label);
	updateValueText();
}

void RotaryControl::setLabel(juce::String text)
{
	setName(text);
	valueLabel.setName(text + " value");
	slider.setName(text);
	slider.setTooltip(text);
	label.setText(std::move(text), juce::dontSendNotification);
}

void RotaryControl::setLayout(Size size, int width)
{
	dialSize = size;
	valueWidth = width;
	resized();
}

void RotaryControl::refreshValueText() { updateValueText(); }

juce::Slider& RotaryControl::getSlider() noexcept
{
	return slider;
}

void RotaryControl::updateValueText()
{
	valueLabel.setColour(juce::Label::outlineColourId, juce::Colour::fromRGB(116, 128, 132));
	valueLabel.setTooltip({});
	valueLabel.setText(slider.getTextFromValue(slider.getValue()), juce::dontSendNotification);
}

void RotaryControl::resized()
{
	const auto bounds = getLocalBounds();
	const auto dialAndValueBounds = bounds.withTrimmedBottom(labelHeight);
	const auto dialBounds = dialAndValueBounds.withTrimmedBottom(valueHeight);
	const auto side = std::max(0, std::min({ heightFor(dialSize) - labelHeight - valueHeight,
		dialBounds.getWidth(), dialBounds.getHeight() }));
	slider.setBounds(dialBounds.withSizeKeepingCentre(side, side));
	valueLabel.setBounds(dialAndValueBounds.withY(dialBounds.getBottom()).withHeight(valueHeight)
		.withSizeKeepingCentre(std::min(valueWidth, bounds.getWidth()), valueHeight));
	label.setBounds(bounds.withY(dialAndValueBounds.getBottom()).withHeight(labelHeight));
}
}
