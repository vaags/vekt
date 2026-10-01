#include <vekt/ui/Oscilloscope.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace vekt::ui
{
namespace
{
const auto leftColour = juce::Colour::fromRGB(227, 156, 75);
const auto rightColour = juce::Colour::fromRGB(232, 225, 208).withAlpha(0.6f);
}

Oscilloscope::Oscilloscope(const dsp::ScopeTap& tap, double window)
	: source(tap), windowSeconds(window), lastWritten(std::numeric_limits<std::uint64_t>::max())
{
	setName("Scope");
	setTooltip("Output waveform, left and right; full height is 0 dBFS");
}

void Oscilloscope::refresh()
{
	const auto rate = source.getSampleRate() > 0.0 ? source.getSampleRate() : 48'000.0;
	const auto length = std::clamp(static_cast<std::size_t>(std::lround(rate * windowSeconds)),
		std::size_t { 2 }, dsp::ScopeTap::capacity / 2);
	// Twice the window: the older half is where the trigger is looked for.
	if (left.size() != length * 2)
	{
		left.assign(length * 2, 0.0f);
		right.assign(length * 2, 0.0f);
		trigger.assign(length * 2, 0.0f);
	}
	const auto written = source.readLatest(left, right);
	if (written == lastWritten && length == windowLength) return;
	lastWritten = written;
	windowLength = length;
	for (std::size_t index = 0; index < trigger.size(); ++index) trigger[index] = left[index] + right[index];
	windowStart = dsp::findScopeTrigger(trigger, windowLength);
	const auto isSilent = [](std::span<const float> samples)
	{
		return std::all_of(samples.begin(), samples.end(), [](float sample) { return juce::exactlyEqual(sample, 0.0f); });
	};
	const auto silent = isSilent(getShownLeft()) && isSilent(getShownRight());
	// A flat line stays a flat line: no need to redraw it every frame.
	if (silent && shownSilent) return;
	shownSilent = silent;
	repaint();
}

std::span<const float> Oscilloscope::getShownLeft() const noexcept
{
	return std::span<const float>(left).subspan(windowStart, windowLength);
}

std::span<const float> Oscilloscope::getShownRight() const noexcept
{
	return std::span<const float>(right).subspan(windowStart, windowLength);
}

void Oscilloscope::paint(juce::Graphics& graphics)
{
	const auto bounds = getLocalBounds().toFloat();
	graphics.setColour(juce::Colour::fromRGB(16, 18, 20));
	graphics.fillRoundedRectangle(bounds, 2.0f);
	const auto plot = bounds.reduced(2.0f);
	const auto yFor = [&](float level)
	{
		return plot.getCentreY() - std::clamp(level, -1.0f, 1.0f) * plot.getHeight() * 0.5f;
	};
	graphics.setColour(juce::Colour::fromRGB(27, 30, 33));
	for (const auto level : { -0.5f, 0.5f })
		graphics.fillRect(plot.getX(), yFor(level) - 0.5f, plot.getWidth(), 1.0f);
	graphics.setColour(juce::Colour::fromRGB(54, 65, 70));
	graphics.fillRect(plot.getX(), plot.getCentreY() - 0.5f, plot.getWidth(), 1.0f);

	graphics.setFont(juce::FontOptions(8.0f).withStyle("Bold"));
	const auto legend = getLocalBounds().reduced(4, 2).removeFromTop(11);
	graphics.setColour(leftColour);
	graphics.drawText("L", legend.withTrimmedRight(10), juce::Justification::centredRight);
	graphics.setColour(rightColour.withAlpha(1.0f));
	graphics.drawText("R", legend, juce::Justification::centredRight);

	if (windowLength == 0) return;
	// One column per pixel, crossing it from its lowest sample to its highest in the order they came, so dense signals
	// keep their extremes and smooth ones stay smooth lines.
	const auto drawTrace = [&](std::span<const float> samples, juce::Colour colour)
	{
		const auto columns = std::max(1, static_cast<int>(plot.getWidth()));
		const auto perColumn = static_cast<double>(samples.size()) / columns;
		juce::Path path;
		for (auto column = 0; column < columns; ++column)
		{
			const auto first = std::min(samples.size() - 1, static_cast<std::size_t>(column * perColumn));
			const auto last = std::clamp(static_cast<std::size_t>((column + 1) * perColumn), first + 1, samples.size());
			const auto [lowest, highest] = std::minmax_element(samples.begin() + static_cast<std::ptrdiff_t>(first),
				samples.begin() + static_cast<std::ptrdiff_t>(last));
			const auto rising = samples[last - 1] >= samples[first];
			const auto x = plot.getX() + static_cast<float>(column);
			const auto from = yFor(rising ? *lowest : *highest);
			const auto to = yFor(rising ? *highest : *lowest);
			if (column == 0) path.startNewSubPath(x, from);
			else path.lineTo(x, from);
			path.lineTo(x + 1.0f, to);
		}
		graphics.setColour(colour);
		graphics.strokePath(path, juce::PathStrokeType(1.25f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
	};
	drawTrace(getShownRight(), rightColour);
	drawTrace(getShownLeft(), leftColour);
}
}
