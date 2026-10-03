#pragma once

#include "DriveStage.h"
#include "FlintEngine.h"

#include <vekt/dsp/DcBlocker.h>
#include <vekt/dsp/LinearRamp.h>

#include <array>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace vekt::flint
{
// One engine slot per model; unimplemented models stay empty and play silence.
using FlintEngines = std::array<std::unique_ptr<FlintEngine>, modelCount>;

// Selects, switches and runs Flint's engines and owns the output stage (docs/FLINT_VALIDATION.md, Signal Path). The
// processor renders the internal (possibly oversampled) rate through renderInternal, downsamples, and finishes at the
// host rate through finishHost. Only the selected engine, and during a switch the one fading out, is ever processed.
class FlintEngineHost final
{
public:
	explicit FlintEngineHost(FlintEngines engines);

	// Sizes every buffer for the largest internal block, so later rate changes never allocate.
	void prepare(double internalSampleRate, int maximumInternalBlockSize, double hostSampleRate);
	// A quality change on the audio thread: re-prepares the engines and Drive at the new internal rate, without
	// allocating, and silences everything.
	void setInternalSampleRate(double internalSampleRate) noexcept;
	// Host samples the downsampling filter keeps sounding after the internal path falls silent.
	void setDownsamplingTail(int hostSamples) noexcept { downsamplingTail = hostSamples; }

	// Once per block. With selectionMayChange false (a preset is being applied) the selected model is held.
	void update(const FlintParameters& parameters, bool selectionMayChange) noexcept;
	void trigger(Strike strike) noexcept;
	void release(int note) noexcept;
	// Writes the next internal-rate samples: the engines, crossfaded during a switch, then Drive.
	void renderInternal(std::span<float> left, std::span<float> right) noexcept;
	// Level, DC blocker and safety clip at the host rate, in place; detects the end of all tails.
	void finishHost(std::span<float> left, std::span<float> right) noexcept;
	// Preset loads and quality changes: silence everything; the next selection change switches at once.
	void resetAudio() noexcept;

	// Nothing sounds and the output is exact zeros until the next strike; the processor may skip all processing.
	[[nodiscard]] bool isSleeping() const noexcept { return sleeping; }
	[[nodiscard]] std::optional<ModelId> selectedModel() const noexcept { return current; }

private:
	[[nodiscard]] FlintEngine* engineAt(std::optional<ModelId> model) const noexcept;
	void select(std::optional<ModelId> target) noexcept;
	void finishFade() noexcept;

	FlintEngines engines;
	FlintParameters parameters;
	std::optional<ModelId> current;
	std::optional<ModelId> fading;
	std::optional<ModelId> pending;
	double currentGain { 1.0 };
	double fadingGain {};
	double fadeStep { 1.0 };
	std::array<std::vector<float>, 4> scratch; // current left/right, fading left/right
	DriveStage drive;
	vekt::dsp::LinearRamp levelGain;
	std::array<vekt::dsp::DcBlocker<double>, 2> dcBlockers;
	int downsamplingTail {};
	int maximumInternalBlock {};
	int tailRemaining {};
	bool internalActive {};
	bool sleeping { true };
};
}
