#pragma once

#include <vekt/flint/Models.h>

#include <array>
#include <cstddef>
#include <optional>

namespace vekt::flint
{
enum class DriveType
{
	soft,
	hard,
	fold
};

// Model controls as the host stores them, 0–1 (docs/FLINT_VALIDATION.md, model sheets); each engine maps its own.
struct KickClassicAnalogSettings
{
	double sweep {};
	double sweepTime { 0.3 };
	double click {};
	double bodyShape {};
};

struct MalletBarSettings
{
	double material { 0.2 };
	double hardness { 0.5 };
	double position { 0.25 };
	double overtones { 1.0 };
	double resonator { 0.5 };
};

// Every Flint parameter, read once per block by the processor and passed to the engine host and the selected engine,
// which reads its own slice (docs/FLINT_VALIDATION.md, Signal Path). Engines never see APVTS.
struct FlintParameters
{
	Mode mode { Mode::kick };
	std::array<int, modeCount> modelIndex {}; // each mode's selected model, an index into modelsOf(mode)

	double pitch { 33.0 }; // MIDI note number, continuous
	double attack { 0.3 };
	double decay { 0.4 };
	double tone { 0.5 };
	double drive {};
	DriveType driveType { DriveType::soft };
	double levelDecibels {};
	double variation { 0.3 };
	bool noteOffDamps {};

	KickClassicAnalogSettings kickClassicAnalog;
	MalletBarSettings malletBar;

	[[nodiscard]] std::optional<ModelId> selectedModel() const noexcept
	{
		return modelAt(mode, modelIndex[static_cast<std::size_t>(mode)]);
	}
};
}
