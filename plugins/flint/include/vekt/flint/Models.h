#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <span>

namespace vekt::flint
{
// Flint's modes and their models (docs/FLINT_VALIDATION.md, Modes and Models): the one list the parameter choices, the
// engine host and the editor share. Lists only grow at their end (ADR 0011): projects and presets store the index.
enum class Mode
{
	kick,
	snare,
	tom,
	clap,
	hiHat,
	cymbal,
	shaker,
	mallet,
	percussion
};

inline constexpr std::array modeNames { "Kick", "Snare", "Tom", "Clap", "Hi-Hat", "Cymbal", "Shaker", "Mallet",
	"Percussion" };
inline constexpr std::size_t modeCount = modeNames.size();

// Every model of every mode, in mode order; the index of an engine in the engine host.
enum class ModelId
{
	kickClassicAnalog,
	kickPunchAnalog,
	kickMembrane,
	kickFm,
	snareClassicAnalog,
	snarePunchAnalog,
	snareMembraneWires,
	snareFmNoise,
	tomClassicAnalog,
	tomPunchAnalog,
	tomMembrane,
	tomHandDrum,
	tomFm,
	clapAnalogBurst,
	clapHandEnsemble,
	hiHatClassicMetal,
	hiHatFmMetal,
	hiHatAcoustic,
	cymbalClassicMetal,
	cymbalAcoustic,
	cymbalFmMetal,
	shakerParticle,
	shakerClassicAnalog,
	malletBar,
	malletPlate,
	malletBell,
	malletFm,
	percussionClassicAnalog,
	percussionWood,
	percussionMetal,
	percussionFm
};

inline constexpr std::size_t modelCount = static_cast<std::size_t>(ModelId::percussionFm) + 1;

struct ModelEntry
{
	ModelId id;
	const char* name;
};

inline constexpr std::array kickModels { ModelEntry { ModelId::kickClassicAnalog, "Classic Analog" },
	ModelEntry { ModelId::kickPunchAnalog, "Punch Analog" }, ModelEntry { ModelId::kickMembrane, "Membrane" },
	ModelEntry { ModelId::kickFm, "FM" } };
inline constexpr std::array snareModels { ModelEntry { ModelId::snareClassicAnalog, "Classic Analog" },
	ModelEntry { ModelId::snarePunchAnalog, "Punch Analog" },
	ModelEntry { ModelId::snareMembraneWires, "Membrane + Wires" },
	ModelEntry { ModelId::snareFmNoise, "FM + Noise" } };
inline constexpr std::array tomModels { ModelEntry { ModelId::tomClassicAnalog, "Classic Analog" },
	ModelEntry { ModelId::tomPunchAnalog, "Punch Analog" }, ModelEntry { ModelId::tomMembrane, "Membrane" },
	ModelEntry { ModelId::tomHandDrum, "Hand Drum" }, ModelEntry { ModelId::tomFm, "FM" } };
inline constexpr std::array clapModels { ModelEntry { ModelId::clapAnalogBurst, "Analog Burst" },
	ModelEntry { ModelId::clapHandEnsemble, "Hand Ensemble" } };
inline constexpr std::array hiHatModels { ModelEntry { ModelId::hiHatClassicMetal, "Classic Metal" },
	ModelEntry { ModelId::hiHatFmMetal, "FM Metal" }, ModelEntry { ModelId::hiHatAcoustic, "Acoustic" } };
inline constexpr std::array cymbalModels { ModelEntry { ModelId::cymbalClassicMetal, "Classic Metal" },
	ModelEntry { ModelId::cymbalAcoustic, "Acoustic" }, ModelEntry { ModelId::cymbalFmMetal, "FM Metal" } };
inline constexpr std::array shakerModels { ModelEntry { ModelId::shakerParticle, "Particle" },
	ModelEntry { ModelId::shakerClassicAnalog, "Classic Analog" } };
inline constexpr std::array malletModels { ModelEntry { ModelId::malletBar, "Bar" },
	ModelEntry { ModelId::malletPlate, "Plate" }, ModelEntry { ModelId::malletBell, "Bell" },
	ModelEntry { ModelId::malletFm, "FM" } };
inline constexpr std::array percussionModels { ModelEntry { ModelId::percussionClassicAnalog, "Classic Analog" },
	ModelEntry { ModelId::percussionWood, "Wood" }, ModelEntry { ModelId::percussionMetal, "Metal" },
	ModelEntry { ModelId::percussionFm, "FM" } };

[[nodiscard]] constexpr std::span<const ModelEntry> modelsOf(Mode mode) noexcept
{
	switch (mode)
	{
	case Mode::kick:
		return kickModels;
	case Mode::snare:
		return snareModels;
	case Mode::tom:
		return tomModels;
	case Mode::clap:
		return clapModels;
	case Mode::hiHat:
		return hiHatModels;
	case Mode::cymbal:
		return cymbalModels;
	case Mode::shaker:
		return shakerModels;
	case Mode::mallet:
		return malletModels;
	case Mode::percussion:
		return percussionModels;
	}
	return {};
}

// The model at a mode's choice index, or nothing for an index outside its list.
[[nodiscard]] constexpr std::optional<ModelId> modelAt(Mode mode, int index) noexcept
{
	const auto models = modelsOf(mode);
	if (index < 0 || static_cast<std::size_t>(index) >= models.size()) return std::nullopt;
	return models[static_cast<std::size_t>(index)].id;
}

[[nodiscard]] constexpr std::size_t indexOf(ModelId id) noexcept { return static_cast<std::size_t>(id); }

// Whether the model has an engine in this build; the others play silence and the editor marks them unavailable.
// makeFlintEngines builds exactly these (tested).
[[nodiscard]] constexpr bool isAvailable(ModelId id) noexcept
{
	return id == ModelId::kickClassicAnalog || id == ModelId::malletBar;
}

// Whether any of the mode's models is available.
[[nodiscard]] constexpr bool isAvailable(Mode mode) noexcept
{
	for (const auto& entry : modelsOf(mode))
		if (isAvailable(entry.id)) return true;
	return false;
}

// The notes a model plays (the model sheets' useful range); it clamps Pitch outside them, and the editor shows the
// note actually played and dims the Pitch arc beyond them.
struct PitchRange
{
	double lowest {};
	double highest {};
};

// Pitch's whole parameter range, MIDI notes C0–C8.
inline constexpr PitchRange fullPitchRange { 12.0, 108.0 };

[[nodiscard]] constexpr PitchRange pitchRangeOf(ModelId id) noexcept
{
	if (id == ModelId::kickClassicAnalog) return { 23.0, 72.0 }; // B0–C5
	if (id == ModelId::malletBar) return { 36.0, 108.0 }; // C2–C8
	return fullPitchRange;
}
}
