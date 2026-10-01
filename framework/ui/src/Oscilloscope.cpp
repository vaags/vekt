#include <vekt/ui/Oscilloscope.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace vekt::ui
{
namespace
{
const auto leftColour = juce::Colour::fromRGB(227, 156, 75);
const auto rightColour = juce::Colour::fromRGB(232, 225, 208).withAlpha(0.6f);
constexpr auto traceThickness = 1.25f;

float yFor(juce::Rectangle<float> plot, float level)
{
	return plot.getCentreY() - std::clamp(level, -1.0f, 1.0f) * plot.getHeight() * 0.5f;
}

// The samples [first, last) drawn in one of columns pixel columns; never empty.
std::pair<std::size_t, std::size_t> columnSamples(std::size_t count, int columns, int column)
{
	const auto perColumn = static_cast<double>(count) / columns;
	const auto first = std::min(count - 1, static_cast<std::size_t>(column * perColumn));
	return { first, std::clamp(static_cast<std::size_t>((column + 1) * perColumn), first + 1, count) };
}
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
	graphics.setColour(juce::Colour::fromRGB(27, 30, 33));
	for (const auto level : { -0.5f, 0.5f })
		graphics.fillRect(plot.getX(), yFor(plot, level) - 0.5f, plot.getWidth(), 1.0f);
	graphics.setColour(juce::Colour::fromRGB(54, 65, 70));
	graphics.fillRect(plot.getX(), plot.getCentreY() - 0.5f, plot.getWidth(), 1.0f);

	graphics.setFont(juce::FontOptions(8.0f).withStyle("Bold"));
	const auto legend = getLocalBounds().reduced(4, 2).removeFromTop(11);
	graphics.setColour(leftColour);
	graphics.drawText("L", legend.withTrimmedRight(10), juce::Justification::centredRight);
	graphics.setColour(rightColour.withAlpha(1.0f));
	graphics.drawText("R", legend, juce::Justification::centredRight);

	if (windowLength == 0) return;
	drawTrace(graphics, plot, getShownRight(), rightColour);
	drawTrace(graphics, plot, getShownLeft(), leftColour);
}

// The trace as one filled band through the pixel column centres: its top edge follows each column's highest sample and
// its bottom edge its lowest (reaching back to the previous column's last sample, so the band never breaks), each
// pushed out by half a line's thickness. Filling it costs a third to a half of stroking a path (whose join and cap
// geometry dominate), with the same smooth edges; one bar per column would be cheaper still but looks pixelated.
void Oscilloscope::drawTrace(juce::Graphics& graphics, juce::Rectangle<float> plot, std::span<const float> samples,
	juce::Colour colour)
{
	const auto columns = std::max(1, static_cast<int>(plot.getWidth()));
	bottoms.resize(static_cast<std::size_t>(columns));
	juce::Path ribbon;
	ribbon.preallocateSpace(columns * 6 + 8);
	for (auto column = 0; column < columns; ++column)
	{
		const auto [first, last] = columnSamples(samples.size(), columns, column);
		const auto from = first > 0 ? first - 1 : first;
		const auto [lowest, highest] = std::minmax_element(samples.begin() + static_cast<std::ptrdiff_t>(from),
			samples.begin() + static_cast<std::ptrdiff_t>(last));
		const auto x = plot.getX() + static_cast<float>(column) + 0.5f;
		const auto top = yFor(plot, *highest) - traceThickness * 0.5f;
		bottoms[static_cast<std::size_t>(column)] = yFor(plot, *lowest) + traceThickness * 0.5f;
		if (column == 0) ribbon.startNewSubPath(x, top);
		else ribbon.lineTo(x, top);
	}
	for (auto column = columns; column-- > 0;)
		ribbon.lineTo(plot.getX() + static_cast<float>(column) + 0.5f, bottoms[static_cast<std::size_t>(column)]);
	ribbon.closeSubPath();
	graphics.setColour(colour);
	graphics.fillPath(ribbon);
}
}
