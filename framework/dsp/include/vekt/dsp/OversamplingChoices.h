#pragma once

#include <vekt/dsp/OversamplingQuality.h>

#include <juce_core/juce_core.h>

namespace vekt::dsp
{
// The oversampling choices every product offers in its Tracking (real-time) and Offline parameters (ADR 0001, 0010).
// Projects store the choice index, so a list only ever grows at its end. Minimum-phase IIR paths stop at 4x.

[[nodiscard]] inline const juce::StringArray& trackingQualityChoices()
{
	static const juce::StringArray choices { "Off", "2x IIR", "4x IIR", "2x FIR", "4x FIR", "8x FIR", "16x FIR" };
	return choices;
}

[[nodiscard]] inline const juce::StringArray& offlineQualityChoices()
{
	static const juce::StringArray choices { "Off", "2x FIR", "4x FIR", "8x FIR", "16x FIR", "2x IIR", "4x IIR" };
	return choices;
}

[[nodiscard]] inline OversamplingQuality trackingQualityFrom(float index) noexcept
{
	switch (juce::roundToInt(index))
	{
	case 0: return { OversamplingFactor::off, OversamplingFilter::polyphaseIIR };
	case 1: return { OversamplingFactor::x2, OversamplingFilter::polyphaseIIR };
	case 2: return { OversamplingFactor::x4, OversamplingFilter::polyphaseIIR };
	case 3: return { OversamplingFactor::x2, OversamplingFilter::polyphaseFIR };
	case 4: return { OversamplingFactor::x4, OversamplingFilter::polyphaseFIR };
	case 5: return { OversamplingFactor::x8, OversamplingFilter::polyphaseFIR };
	default: return { OversamplingFactor::x16, OversamplingFilter::polyphaseFIR };
	}
}

[[nodiscard]] inline OversamplingQuality offlineQualityFrom(float index) noexcept
{
	switch (juce::roundToInt(index))
	{
	case 0: return { OversamplingFactor::off, OversamplingFilter::polyphaseIIR };
	case 1: return { OversamplingFactor::x2, OversamplingFilter::polyphaseFIR };
	case 2: return { OversamplingFactor::x4, OversamplingFilter::polyphaseFIR };
	case 3: return { OversamplingFactor::x8, OversamplingFilter::polyphaseFIR };
	case 4: return { OversamplingFactor::x16, OversamplingFilter::polyphaseFIR };
	case 5: return { OversamplingFactor::x2, OversamplingFilter::polyphaseIIR };
	default: return { OversamplingFactor::x4, OversamplingFilter::polyphaseIIR };
	}
}

// How editors name a quality, matching the choice lists: "Off", "2x IIR", "16x FIR".
[[nodiscard]] inline juce::String qualityName(OversamplingQuality quality)
{
	if (quality.factor == OversamplingFactor::off)
		return "Off";
	return juce::String(static_cast<int>(quality.multiplier())) + "x "
		+ (quality.filter == OversamplingFilter::polyphaseFIR ? "FIR" : "IIR");
}
}
