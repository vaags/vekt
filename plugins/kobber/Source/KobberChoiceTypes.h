#pragma once

namespace vekt::kobber
{
// The values Mono's choice parameters select (KobberParameterChoices.h), apart from the LFO's (Lfo.h). Kept apart from
// the voice so the parameter layout and the editor need not include it.

// The filter topology: Ladder (ADR 0005), SVF (ADR 0006) or K35 (ADR 0007), read per render segment from filterType.
enum class FilterType
{
	ladder,
	svf,
	korg35
};
enum class GlideMode
{
	off,
	always,
	legato
}; // Legato glides only between overlapping notes
enum class NoiseType
{
	off,
	white,
	pink
};
enum class PerformanceMode
{
	poly,
	mono,
	monoLegato
};
enum class NotePriority
{
	last,
	low
};
}
