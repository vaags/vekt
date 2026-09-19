#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace vekt::ui
{
class UndoRedoControls final : public juce::Component
{
public:
	explicit UndoRedoControls(juce::UndoManager& manager) : history(manager)
	{
		setName("History");
		undoButton.setName("Undo");
		redoButton.setName("Redo");
		undoButton.setTooltip("Undo");
		redoButton.setTooltip("Redo");
		addAndMakeVisible(undoButton);
		addAndMakeVisible(redoButton);
		undoButton.onClick = [this] { perform(false); };
		redoButton.onClick = [this] { perform(true); };
		refresh();
	}

	std::function<void()> beforeAction;
	std::function<void()> onChange;

	void refresh()
	{
		undoButton.setEnabled(history.canUndo());
		redoButton.setEnabled(history.canRedo());
		undoButton.setTooltip(history.canUndo() ? "Undo " + history.getUndoDescription() : "Undo");
		redoButton.setTooltip(history.canRedo() ? "Redo " + history.getRedoDescription() : "Redo");
	}

	void resized() override
	{
		const auto buttonWidth = juce::jmax(0, (getWidth() - 8) / 2);
		undoButton.setBounds(0, 0, buttonWidth, getHeight());
		redoButton.setBounds(buttonWidth + 8, 0, buttonWidth, getHeight());
	}

private:
	class HistoryButton final : public juce::Button
	{
	public:
		explicit HistoryButton(bool redo) : Button(redo ? "Redo" : "Undo"), pointsRight(redo) {}

		void paintButton(juce::Graphics& graphics, bool hovered, bool pressed) override
		{
			getLookAndFeel().drawButtonBackground(graphics, *this,
				findColour(juce::TextButton::buttonColourId), hovered, pressed);
			juce::Path arrow;
			arrow.startNewSubPath(5.0f, 8.0f);
			arrow.cubicTo(19.0f, 3.0f, 23.0f, 18.0f, 12.0f, 19.0f);
			arrow.startNewSubPath(9.0f, 3.0f);
			arrow.lineTo(5.0f, 8.0f);
			arrow.lineTo(10.0f, 12.0f);
			if (pointsRight) arrow.applyTransform(juce::AffineTransform::scale(-1.0f, 1.0f).translated(24.0f, 0.0f));
			arrow.applyTransform(juce::AffineTransform::translation(
				(static_cast<float>(getWidth()) - 24.0f) * 0.5f, (static_cast<float>(getHeight()) - 24.0f) * 0.5f));
			graphics.setColour(findColour(juce::TextButton::textColourOffId).withMultipliedAlpha(isEnabled() ? 1.0f : 0.4f));
			graphics.strokePath(arrow, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
			if (hasKeyboardFocus(true)) graphics.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 4.0f, 2.0f);
		}

	private:
		bool pointsRight;
	};

	void perform(bool redo)
	{
		if (beforeAction) beforeAction();
		if (redo) history.redo(); else history.undo();
		history.beginNewTransaction();
		if (onChange) onChange();
		refresh();
	}

	juce::UndoManager& history;
	HistoryButton undoButton { false };
	HistoryButton redoButton { true };
};
}