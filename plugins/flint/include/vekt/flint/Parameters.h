#pragma once

#include <vekt/flint/Models.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <span>

namespace vekt::flint::parameters
{
inline constexpr auto stateType = "FlintParameters";
inline constexpr auto presetProductIdentifier = "com.vekt.flint";
// The product name hosts, preset banks and the user preset folder show.
inline constexpr auto productName = "Flint";
// The preset sound schema this build writes and loads.
inline constexpr int presetSoundSchemaVersion = 1;
inline constexpr int version = 1;

// Shared controls (docs/FLINT_VALIDATION.md, Parameters).
inline constexpr auto pitch = "flint.pitch";
inline constexpr auto attack = "flint.attack";
inline constexpr auto decay = "flint.decay";
inline constexpr auto tone = "flint.tone";
inline constexpr auto drive = "flint.drive";
inline constexpr auto driveType = "flint.driveType";
inline constexpr auto level = "flint.level";
inline constexpr auto velocity = "flint.velocity";
inline constexpr auto variation = "flint.variation";
inline constexpr auto noteOffDamps = "flint.noteOffDamps";
// Oversampling for real-time playback and for offline rendering, the same choices as every product (ADR 0010).
// Saved with the plugin state, not in presets; not automatable.
inline constexpr auto trackingOversampling = "flint.trackingOversampling";
inline constexpr auto offlineOversampling = "flint.offlineOversampling";

// Selection: not automatable; the lists only grow at their end (ADR 0011).
inline constexpr auto mode = "flint.mode";
inline constexpr auto kickModel = "flint.kick.model";
inline constexpr auto malletModel = "flint.mallet.model";

// Kick / Classic Analog.
inline constexpr auto kickSweep = "flint.kick.analog.sweep";
inline constexpr auto kickSweepTime = "flint.kick.analog.sweepTime";
inline constexpr auto kickClick = "flint.kick.analog.click";
inline constexpr auto kickBodyShape = "flint.kick.analog.bodyShape";

// Mallet / Bar.
inline constexpr auto barMaterial = "flint.mallet.bar.material";
inline constexpr auto barHardness = "flint.mallet.bar.hardness";
inline constexpr auto barPosition = "flint.mallet.bar.position";
inline constexpr auto barOvertones = "flint.mallet.bar.overtones";
inline constexpr auto barResonator = "flint.mallet.bar.resonator";

// A model's own controls in the editor's model slots, in order (the model sheets).
struct ModelControl
{
	const char* identifier;
	const char* name;
};

inline constexpr std::size_t maximumModelControls = 5;
inline constexpr std::array kickClassicAnalogControls { ModelControl { kickSweep, "Sweep" },
	ModelControl { kickSweepTime, "Sweep Time" }, ModelControl { kickClick, "Click" },
	ModelControl { kickBodyShape, "Body Shape" } };
inline constexpr std::array malletBarControls { ModelControl { barMaterial, "Material" },
	ModelControl { barHardness, "Hardness" }, ModelControl { barPosition, "Position" },
	ModelControl { barOvertones, "Overtones" }, ModelControl { barResonator, "Resonator" } };
static_assert(kickClassicAnalogControls.size() <= maximumModelControls);
static_assert(malletBarControls.size() <= maximumModelControls);

[[nodiscard]] constexpr std::span<const ModelControl> modelControlsOf(ModelId id) noexcept
{
	if (id == ModelId::kickClassicAnalog) return kickClassicAnalogControls;
	if (id == ModelId::malletBar) return malletBarControls;
	return {};
}

// The parameter holding a mode's model choice, or nullptr for a mode that has none yet (its models arrive later).
[[nodiscard]] constexpr const char* modelParameterOf(Mode selected) noexcept
{
	if (selected == Mode::kick) return kickModel;
	if (selected == Mode::mallet) return malletModel;
	return nullptr;
}

// Every parameter a preset stores: all but the quality settings.
inline constexpr std::array soundParameterIds { pitch, attack, decay, tone, drive, driveType, level, velocity,
	variation, noteOffDamps, mode, kickModel, malletModel, kickSweep, kickSweepTime, kickClick, kickBodyShape,
	barMaterial, barHardness, barPosition, barOvertones, barResonator };

// The shared controls' values a Mode change in the editor sets, in parameter units: each mode's classic sound
// (docs/FLINT_VALIDATION.md, Parameters). Parameter defaults are the Kick's, the default mode.
struct ModeStartValues
{
	float pitch {};
	float attack {};
	float decay {};
	float tone {};
};

[[nodiscard]] constexpr ModeStartValues startValuesFor(Mode selected) noexcept
{
	switch (selected)
	{
	case Mode::mallet:
		return { 60.0f, 30.0f, 45.0f, 50.0f };
	case Mode::kick:
	case Mode::snare:
	case Mode::tom:
	case Mode::clap:
	case Mode::hiHat:
	case Mode::cymbal:
	case Mode::shaker:
	case Mode::percussion:
		break;
	}
	return { 33.0f, 30.0f, 40.0f, 50.0f };
}

[[nodiscard]] juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
// Pitch's host text: note and cents, "D#2 +12 ct".
[[nodiscard]] juce::String pitchText(float note);
[[nodiscard]] float pitchFromText(const juce::String& text);
}
