#pragma once

#include "FlintParameters.h"

#include <cstdint>
#include <span>

namespace vekt::flint
{
// One hit. The note matters only to models that read it (Hi-Hat Two-Note Mode, Note Off damping), never for pitch.
// The engine draws as many random values as it needs from the hit hash (docs/FLINT_VALIDATION.md, Shared stages).
struct Strike
{
	double velocity {}; // 0–1, after the velocity sensitivity
	int note {};
	std::uint64_t hash {};
};

// A synthesis model behind the engine host (docs/FLINT_VALIDATION.md, Signal Path). The host calls activate when the
// engine becomes selected, update once per block, and trigger, release and process per block segment between MIDI
// events; it calls process only while isActive() is true.
class FlintEngine
{
public:
	FlintEngine() = default;
	virtual ~FlintEngine() = default;
	FlintEngine(const FlintEngine&) = delete;
	FlintEngine& operator=(const FlintEngine&) = delete;
	FlintEngine(FlintEngine&&) = delete;
	FlintEngine& operator=(FlintEngine&&) = delete;

	// Called again from the audio thread when the quality changes: it must not allocate.
	virtual void prepare(double sampleRate, int maximumBlockSize) noexcept = 0;
	virtual void reset() noexcept = 0;
	// Becoming selected: smoothing starts at these values rather than ramping from stale ones.
	virtual void activate(const FlintParameters& parameters) noexcept = 0;
	virtual void update(const FlintParameters& parameters) noexcept = 0;
	virtual void trigger(Strike strike) noexcept = 0;
	virtual void release(int note) noexcept = 0;
	// Writes (does not add) the next samples to both channels.
	virtual void process(std::span<float> left, std::span<float> right) noexcept = 0;
	[[nodiscard]] virtual bool isActive() const noexcept = 0;
	[[nodiscard]] virtual double energy() const noexcept = 0;
};
}
