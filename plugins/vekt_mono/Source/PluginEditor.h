#pragma once

#include <vekt/mono/PluginProcessor.h>

#include "LfoDestinations.h"
#include "Lfo.h"
#include <vekt/ui/LevelMeter.h>
#include <vekt/ui/Oscilloscope.h>
#include <vekt/ui/Panel.h>
#include <vekt/ui/PresetNavigation.h>
#include <vekt/ui/RotaryControl.h>
#include <vekt/ui/ScalableEditor.h>
#include <vekt/ui/UndoRedoControls.h>
#include <vekt/ui/VektLookAndFeel.h>
#include <vekt/preset_ui/PresetBrowser.h>

#include <algorithm>
#include <array>
#include <memory>
#include <span>

namespace vekt::mono
{
// A selector tab in a panel header (the LFO and filter-type tabs): the selected tab is lit and outlined.
inline void paintHeaderTab(juce::Graphics& graphics, juce::Button& button, bool hovered, bool pressed,
	juce::Rectangle<int> textArea)
{
	const auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
	const auto fill = button.getToggleState() ? juce::Colour::fromRGB(58, 66, 68) : juce::Colour::fromRGB(31, 36, 38);
	graphics.setColour(pressed ? fill.brighter(0.12f) : hovered ? fill.brighter(0.06f) : fill);
	graphics.fillRoundedRectangle(bounds, 4.0f);
	graphics.setColour(button.getToggleState() ? juce::Colour::fromRGB(227, 156, 75) : juce::Colour::fromRGB(75, 84, 87));
	graphics.drawRoundedRectangle(bounds, 4.0f, button.hasKeyboardFocus(true) ? 2.0f : 1.0f);
	graphics.setColour(juce::Colour::fromRGB(224, 226, 220));
	graphics.setFont(juce::FontOptions(14.0f).withStyle("Bold"));
	graphics.drawText(button.getButtonText(), textArea, juce::Justification::centred);
}

// Selects which LFO the LFO panel shows; its dot glows with that LFO's live output.
class LfoTabButton final : public juce::Button
{
public:
	LfoTabButton() : Button({}) { setClickingTogglesState(true); }
	void setLevel(float newLevel)
	{
		if (juce::approximatelyEqual(level, newLevel)) return;
		level = newLevel;
		repaint();
	}
	void paintButton(juce::Graphics& graphics, bool hovered, bool pressed) override
	{
		paintHeaderTab(graphics, *this, hovered, pressed, getLocalBounds().withTrimmedRight(16));
		const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
		const auto dot = juce::Rectangle<float>(bounds.getRight() - 16.0f, bounds.getCentreY() - 4.0f, 8.0f, 8.0f);
		graphics.setColour(juce::Colour::fromRGB(227, 156, 75).withAlpha(0.2f + 0.8f * std::min(1.0f, std::abs(level))));
		graphics.fillEllipse(dot);
	}

private:
	float level {};
};

// One filter type (Ladder, SVF or K35) in the filter panel's header.
class FilterTypeTab final : public juce::Button
{
public:
	FilterTypeTab() : Button({}) {}
	void paintButton(juce::Graphics& graphics, bool hovered, bool pressed) override
	{
		paintHeaderTab(graphics, *this, hovered, pressed, getLocalBounds());
	}
};

// The on-screen mod wheel, attached to Vibrato Amount (0..100 %): drag it, or focus it and use the arrow keys (Shift
// for fine steps). The bar fills to the amount in effect, the higher of the wheel and the live mod wheel / aftertouch;
// the handle shows where the wheel itself sits.
class VibratoWheel final : public juce::Slider
{
public:
	VibratoWheel()
	{
		setSliderStyle(LinearHorizontal);
		setTextBoxStyle(NoTextBox, true, 0, 0);
		setWantsKeyboardFocus(true);
		// Shift-drag moves relative to the press, at a fifth of the speed.
		setVelocityModeParameters(0.2, 1, 0.0, true, juce::ModifierKeys::shiftModifier);
	}
	// The live amount in effect (0..1), from the processor.
	void setLevel(float newLevel)
	{
		if (juce::approximatelyEqual(level, newLevel)) return;
		level = newLevel;
		repaint();
	}
	[[nodiscard]] float getLevel() const noexcept { return level; }
	bool keyPressed(const juce::KeyPress& key) override
	{
		const auto code = key.getKeyCode();
		const auto direction = code == juce::KeyPress::rightKey || code == juce::KeyPress::upKey ? 1.0
			: code == juce::KeyPress::leftKey || code == juce::KeyPress::downKey ? -1.0 : 0.0;
		const auto modifiers = key.getModifiers();
		if (direction == 0.0 || modifiers.isCommandDown() || modifiers.isAltDown() || modifiers.isCtrlDown())
			return Slider::keyPressed(key);
		setValue(getValue() + direction * (modifiers.isShiftDown() ? 0.1 : 1.0), juce::sendNotificationSync);
		return true;
	}
	void paint(juce::Graphics& graphics) override
	{
		const auto centreY = static_cast<float>(getHeight()) * 0.5f;
		const auto left = static_cast<float>(getPositionOfValue(getMinimum()));
		const auto right = static_cast<float>(getPositionOfValue(getMaximum()));
		const auto track = juce::Rectangle<float>::leftTopRightBottom(left, centreY - 6.0f, right, centreY + 6.0f).reduced(0.5f);
		const auto wheel = static_cast<float>(valueToProportionOfLength(getValue()));
		graphics.setColour(juce::Colour::fromRGB(19, 24, 27));
		graphics.fillRoundedRectangle(track, 3.0f);
		graphics.setColour(juce::Colour::fromRGB(227, 156, 75));
		graphics.fillRoundedRectangle(track.withWidth(track.getWidth() * std::clamp(std::max(level, wheel), 0.0f, 1.0f)), 3.0f);
		const auto focused = hasKeyboardFocus(false);
		graphics.setColour(focused ? juce::Colour::fromRGB(227, 156, 75) : juce::Colour::fromRGB(70, 82, 86));
		graphics.drawRoundedRectangle(track, 3.0f, focused ? 2.0f : 1.0f);
		const auto handle = static_cast<float>(getPositionOfValue(getValue()));
		graphics.setColour(juce::Colour::fromRGB(224, 226, 220).withMultipliedAlpha(isMouseOverOrDragging() || focused ? 1.0f : 0.8f));
		graphics.fillRoundedRectangle(juce::Rectangle<float>(handle - 2.5f, centreY - 9.0f, 5.0f, 18.0f), 2.0f);
	}

private:
	float level {};
};

// How fast one LFO at most moves a destination's dot, as a share of the knob's travel per second: pi x rate x its
// swing, a sine's peak speed, which other continuous shapes do not much exceed (a triangle's is 2 x rate x swing; saw
// and square edges are jumps, not motion). The dot's visibility follows this speed (ModulationRing::dotOpacityForSpeed),
// so a deep slow LFO and a shallow fast one can both keep their dot.
[[nodiscard]] double lfoPeakTravelPerSecond(const LfoDestination& destination, const juce::NormalisableRange<float>& knob,
	double base, float offset, LfoPolarity polarity, float rateHz) noexcept;

// One LFO's part in a destination's live dot: its offset at full output, its current output, polarity and opacity.
struct LfoDotContribution
{
	float offset {}, output {};
	LfoPolarity polarity { LfoPolarity::bipolar };
	float opacity { 1.0f };
};
struct LfoDot
{
	// The dot's offset, its opacity, and the half-width of the blur band around it, all in destination units.
	float offset {}, opacity {}, blurHalfWidth {};
};
// Combines the LFOs reaching one destination into the dot and its blur band. The dot is as clear as its slowest LFO
// allows. Each LFO moves it by the share it can be followed (its opacity); for the rest it stands at the centre of its
// swing and widens the band by that share of its swing instead, so the value heard always lies within the band. The
// two cross-fade: as an LFO speeds up, the band grows as its dot fades, never leaving neither. Beside a slow LFO, a
// fast one shows as a band riding on the slow dot; LFOs all too fast to follow leave only a band over their range.
[[nodiscard]] LfoDot combineLfoDot(std::span<const LfoDotContribution> contributions) noexcept;

class PluginEditor final : public ui::ScalableEditor, private juce::Timer
{
public:
	// Mono is wider than the other products: the extra column holds the LFO (and later vibrato) panels.
	static constexpr int editorWidth = 1484;

	explicit PluginEditor(PluginProcessor& processor);
	~PluginEditor() override;
	void paint(juce::Graphics&) override;
	void resized() override;
	// Shows each LFO destination's reachable range on its knob, from the depths, Amounts and polarities, with a dot
	// at the newest sounding voice's live value. Runs every display frame while the editor shows, with the frame's
	// presentation time; the LFO values come from a short, self-adjusting delay back, so they move smoothly whatever
	// the host's block size.
	void refreshModulationRings(double nowSeconds);

private:
	using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
	using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
	using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
	void timerCallback() override;
	void refreshPresetLabel();
	void addRotary(ui::Panel& panel, ui::RotaryControl& control, const char* name, const char* identifier,
		std::unique_ptr<SliderAttachment>& attachment);
	void addChoice(ui::Panel& panel, juce::ComboBox& box, const juce::StringArray& choices, const char* identifier,
		std::unique_ptr<ComboBoxAttachment>& attachment);
	void selectLfo(std::size_t index);
	// Shows the filter type (0 Ladder, 1 SVF): lights its tab and disables the Ladder-only toggles for the SVF.
	void refreshFilterType();
	void selectFilterType(int type);
	void refreshLfoVisibility();

	struct LfoControls
	{
		juce::ComboBox shape, polarity, mode;
		juce::ToggleButton sync { "Sync" };
		ui::RotaryControl rate, division, amount, phase, delay, fade;
		// Same order as parameters::LfoParameterIds::depths().
		std::array<juce::Slider, 19> depths;
		std::unique_ptr<ComboBoxAttachment> shapeAttachment, polarityAttachment, modeAttachment;
		std::unique_ptr<ButtonAttachment> syncAttachment;
		std::array<std::unique_ptr<SliderAttachment>, 6> knobAttachments;
		std::array<std::unique_ptr<SliderAttachment>, 19> depthAttachments;
	};

	PluginProcessor& pluginProcessor;
	ui::VektLookAndFeel lookAndFeel;
	juce::Label title;
	juce::Label status;
	ui::UndoRedoControls historyControls;
	ui::PresetNavigation presetNavigation;
	preset_ui::PresetBrowser presetBrowser;
	std::array<ui::Panel, 3> oscillatorPanels { ui::Panel { "Osc 1" }, ui::Panel { "Osc 2" }, ui::Panel { "Osc 3" } };
	ui::Panel noisePanel { "Noise" };
	ui::Panel filterPanel { "Filter" };
	ui::Panel voicePanel { "Voice" };
	ui::Panel ioPanel { "I/O" };
	ui::Panel ampPanel { "Amp ADSR" };
	ui::Panel filterEnvelopePanel { "Filter ADSR" };
	ui::Panel performancePanel { "Performance" };
	ui::Panel lfoPanel { "LFO" };
	ui::Panel vibratoPanel { "Vibrato" };
	ui::RotaryControl vibratoRateControl, vibratoDepthControl;
	std::unique_ptr<SliderAttachment> vibratoRateAttachment, vibratoDepthAttachment;
	juce::ComboBox vibratoShapeBox;
	std::unique_ptr<ComboBoxAttachment> vibratoShapeAttachment;
	juce::Label vibratoShapeLabel, vibratoMeterLabel;
	VibratoWheel vibratoMeter;
	std::unique_ptr<SliderAttachment> vibratoAmountAttachment;
	std::array<LfoTabButton, 2> lfoTabs;
	std::array<LfoControls, 2> lfoControls;
	// Column headers (Pitch, Morph, Width, Level), oscillator rows, then the seven single destinations.
	std::array<juce::Label, 14> lfoDestinationLabels;
	std::size_t selectedLfo {};
	// The knob each LFO destination moves, in depths() order; nullptr where no knob shows it (Amp).
	std::array<ui::RotaryControl*, 19> lfoTargets {};
	std::array<ui::RotaryControl, 15> oscillatorControls;
	std::array<std::unique_ptr<SliderAttachment>, 15> oscillatorAttachments;
	ui::RotaryControl noiseLevelControl;
	std::unique_ptr<SliderAttachment> noiseLevelAttachment;
	juce::Label noiseTypeLabel;
	std::array<ui::RotaryControl, 6> filterControls;
	std::array<std::unique_ptr<SliderAttachment>, 6> filterAttachments;
	std::array<FilterTypeTab, 3> filterTypeTabs;
	juce::ToggleButton qCompensationButton { "Q Compensation" };
	std::unique_ptr<ButtonAttachment> qCompensationAttachment;
	// After the tabs and toggles their callbacks update, so they are destroyed before them.
	std::unique_ptr<juce::ParameterAttachment> filterTypeAttachment;
	// The value the attachment last delivered (its callback can run before the parameter state's raw value moves).
	int shownFilterType {};
	std::array<ui::RotaryControl, 5> ampControls;
	std::array<std::unique_ptr<SliderAttachment>, 5> ampAttachments;
	std::array<ui::RotaryControl, 5> filterEnvelopeControls;
	std::array<std::unique_ptr<SliderAttachment>, 5> filterEnvelopeAttachments;
	std::array<ui::RotaryControl, 5> voiceControls;
	std::array<std::unique_ptr<SliderAttachment>, 5> voiceAttachments;
	juce::Slider outputFader;
	ui::LevelMeter outputMeter { "OUT", juce::Colour::fromRGB(227, 156, 75), ui::LevelMeter::Orientation::horizontal };
	ui::Oscilloscope outputScope { pluginProcessor.getOutputScope() };
	std::unique_ptr<SliderAttachment> outputAttachment;
	juce::ComboBox voiceCountBox, performanceModeBox, qualityBox, unisonBox, noiseBox, glideBox, priorityBox, multicoreBox;
	juce::ToggleButton heldKeyReturnButton { "Held return" };
	std::array<juce::Label, 6> performanceLabels;
	juce::Label activeVoicesLabel;
	std::unique_ptr<ComboBoxAttachment> voiceCountAttachment, performanceModeAttachment, qualityAttachment, unisonAttachment, noiseAttachment, glideAttachment, priorityAttachment, multicoreAttachment;
	std::unique_ptr<ButtonAttachment> heldKeyReturnAttachment;
	dsp::DisplayTimeline<2> lfoTimeline;
	// Moves the modulation dots once per display frame while the editor is showing. Last, so it stops first.
	juce::VBlankAttachment modulationRefresh { this, [this](double frameSeconds) { refreshModulationRings(frameSeconds); } };
};
}
