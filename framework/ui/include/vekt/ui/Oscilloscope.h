#pragma once

#include <vekt/dsp/ScopeTap.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <span>
#include <vector>

namespace vekt::ui
{
// Draws a ScopeTap's left and right waveforms over a fixed time window, full scale at 0 dBFS, triggered on rising
// zero crossings so periodic signals stand still. Refreshes itself every display frame while showing; the tap must
// outlive it (the processor owns it, the editor this).
class Oscilloscope final : public juce::Component, public juce::SettableTooltipClient
{
public:
	explicit Oscilloscope(const dsp::ScopeTap& source, double windowSeconds = 0.025);

	// Reads the newest samples and repaints if they changed. Called every display frame; public for tests.
	void refresh();
	// The samples currently shown (windowLength of each channel, from the trigger onwards).
	[[nodiscard]] std::span<const float> getShownLeft() const noexcept;
	[[nodiscard]] std::span<const float> getShownRight() const noexcept;
	void paint(juce::Graphics&) override;

private:
	void drawTrace(juce::Graphics&, juce::Rectangle<float> plot, std::span<const float> samples, juce::Colour);

	const dsp::ScopeTap& source;
	double windowSeconds;
	std::vector<float> left, right, trigger;
	// drawTrace's bottom edge, kept to avoid allocating per frame.
	std::vector<float> bottoms;
	std::size_t windowLength {}, windowStart {};
	std::uint64_t lastWritten {};
	bool shownSilent { true };
	// Last, so it stops before the rest is destroyed.
	juce::VBlankAttachment refreshAttachment { this, [this] { refresh(); } };
};
}
