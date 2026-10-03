#pragma once

#include "FilterLimits.h"
#include "Lfo.h"

#include <vekt/kobber/Parameters.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

namespace vekt::kobber
{
// Indices into parameters::LfoParameterIds::depths(); the per-oscillator destinations add the oscillator (0..2).
namespace lfo_depth
{
inline constexpr std::size_t pitch = 0, morph = 3, width = 6, level = 9;
inline constexpr std::size_t filter = 12, amp = 13, drive = 14, noise = 15, detune = 16, spread = 17, filterMode = 18;
}

enum class LfoTargetScale { linear, octaves };
// Where the voice stops the modulated value: at the knob's own range, nowhere, or at the filter's cutoff limits.
enum class LfoTargetLimit { knob, unbounded, cutoff };

// How one LFO depth reaches the sound. At full Amount and full LFO output, a depth d moves its destination by
// d * offsetPerDepth in the voice's own units (KobberModulation); the processor applies exactly these factors. For
// display, `target` is the knob whose value that offset moves. A linear target moves by offset * targetPerOffset in
// the knob's units; an octaves target is multiplied by 2^offset.
struct LfoDestination
{
	float offsetPerDepth {};
	const char* target {}; // nullptr: no knob shows this destination
	LfoTargetScale scale { LfoTargetScale::linear };
	float targetPerOffset { 1.0f };
	LfoTargetLimit limit { LfoTargetLimit::knob };
};

namespace detail
{
// Pitch in semitones, shown on the Octave knob. Unbounded: it carries on past the knob's two octaves.
constexpr LfoDestination pitch(const char* octave) { return { 1.0f, octave, LfoTargetScale::linear, 1.0f / 12.0f, LfoTargetLimit::unbounded }; }
// Morph in cycle units (0..4): 100 % is one full turn. Never stopped: it wraps round the cycle, as its knob does.
constexpr LfoDestination morph(const char* knob) { return { 0.04f, knob, LfoTargetScale::linear, 1.0f, LfoTargetLimit::unbounded }; }
// Pulse width in percentage points: 100 % is +/-45 points, the whole 5..95 % range.
constexpr LfoDestination width(const char* knob) { return { 0.45f, knob, LfoTargetScale::linear, 1.0f }; }
// A 0..1 voice amount behind a 0..100 % knob.
constexpr LfoDestination fraction(const char* knob) { return { 0.01f, knob, LfoTargetScale::linear, 100.0f }; }
}

// In parameters::LfoParameterIds::depths() order.
inline constexpr std::array<LfoDestination, 19> lfoDestinations { {
	detail::pitch(parameters::osc1Octave), detail::pitch(parameters::osc2Octave), detail::pitch(parameters::osc3Octave),
	detail::morph(parameters::osc1Morph), detail::morph(parameters::osc2Morph), detail::morph(parameters::osc3Morph),
	detail::width(parameters::osc1PulseWidth), detail::width(parameters::osc2PulseWidth), detail::width(parameters::osc3PulseWidth),
	detail::fraction(parameters::osc1Level), detail::fraction(parameters::osc2Level), detail::fraction(parameters::osc3Level),
	{ 1.0f, parameters::filterCutoff, LfoTargetScale::octaves, 1.0f, LfoTargetLimit::cutoff }, // octaves
	{ 0.01f, nullptr }, // Amp scales the amp envelope's output; it has no knob of its own
	{ 1.0f, parameters::filterDrive }, // dB
	detail::fraction(parameters::noiseLevel),
	{ 0.5f, parameters::unisonDetune }, // cents: 100 % is +/-50 ct, the whole Detune range
	detail::fraction(parameters::unisonSpread),
	{ 0.02f, parameters::filterMode }, // 100 % sweeps the whole Mode range, LP to HP
} };
static_assert(lfoDestinations.size() == parameters::lfos[0].depths().size());

// The offsets (destination units) that LFOs together can reach on one destination, before clamping or wrapping.
struct LfoReach
{
	float lowest {}, highest {};

	// Adds one LFO whose full output moves the destination by `offset`: a bipolar LFO swings both ways, a unipolar
	// one only towards the offset's sign.
	void add(float offset, LfoPolarity polarity) noexcept
	{
		if (polarity == LfoPolarity::bipolar)
		{
			lowest -= std::abs(offset);
			highest += std::abs(offset);
		}
		else if (offset < 0.0f) lowest += offset;
		else highest += offset;
	}
	// lowest never rises above zero nor highest falls below it.
	[[nodiscard]] bool isEmpty() const noexcept { return !(lowest < 0.0f) && !(highest > 0.0f); }
};

// The values the voice can actually reach on a destination's knob, in the knob's units. Knob-limited destinations
// stop where the knob does; the others may pass either end of its travel.
struct LfoTargetBounds
{
	double lowest {}, highest {};
	[[nodiscard]] double clamp(double value) const noexcept { return std::clamp(value, lowest, highest); }
};
[[nodiscard]] inline LfoTargetBounds lfoTargetBounds(const LfoDestination& destination, double knobMinimum, double knobMaximum,
	double sampleRate) noexcept
{
	switch (destination.limit)
	{
	case LfoTargetLimit::unbounded: return { -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity() };
	case LfoTargetLimit::cutoff: return { minimumCutoffHz, maximumCutoffHz(sampleRate) };
	case LfoTargetLimit::knob: break;
	}
	return { knobMinimum, knobMaximum };
}

// The target knob's value with `offset` applied to `base`, before clamping or wrapping.
[[nodiscard]] inline double lfoTargetValue(const LfoDestination& destination, double base, float offset) noexcept
{
	if (destination.scale == LfoTargetScale::octaves) return base * std::exp2(static_cast<double>(offset));
	return base + static_cast<double>(offset * destination.targetPerOffset);
}
}
