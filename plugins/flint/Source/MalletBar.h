#pragma once

#include "FlintEngine.h"
#include "HitRandom.h"
#include "ModalBank.h"

#include <vekt/dsp/LinearRamp.h>
#include <vekt/dsp/LinearTptSvf.h>

#include <array>

namespace vekt::flint
{
// Mallet / Bar (docs/FLINT_VALIDATION.md, Bar sheet): a struck tuned bar of up to 12 modes, from rosewood marimba to
// glockenspiel. Overtones moves modes 2 and 3 from a uniform free-free bar through xylophone (1:3:6) to marimba
// (1:4:10); a Hertz-scaled contact pulse strikes it at Position; Material sets how strongly high modes are
// damped; a tube resonance at the fundamental can follow in series; a noise "tock" gives each hit its own transient.
class MalletBar final : public FlintEngine
{
public:
	static constexpr std::size_t barModes = 12;

	void prepare(double sampleRate, int maximumBlockSize) noexcept override;
	void reset() noexcept override;
	void activate(const FlintParameters& parameters) noexcept override;
	void update(const FlintParameters& parameters) noexcept override;
	void trigger(Strike strike) noexcept override;
	void release(int note) noexcept override;
	void process(std::span<float> left, std::span<float> right) noexcept override;
	[[nodiscard]] bool isActive() const noexcept override;
	[[nodiscard]] double energy() const noexcept override;
	// Whether the tube resonance holds any energy (tests: it never runs at Resonator 0).
	[[nodiscard]] bool tubeRinging() const noexcept { return tube.isActive(); }

	// The sheet's mappings.
	[[nodiscard]] static double pitchHz(double note) noexcept; // clamped to C2–C8
	[[nodiscard]] static std::array<double, barModes> ratios(double overtones) noexcept;
	// Mode n's free-free shape at the strike point for Position 0–1, relative to the free end.
	[[nodiscard]] static double modeShape(std::size_t mode, double strikePoint) noexcept;
	[[nodiscard]] static double strikePoint(double position) noexcept;
	[[nodiscard]] static double fundamentalT60(double decay, double fundamentalHz) noexcept;
	[[nodiscard]] static double contactSeconds(double hardness) noexcept;
	// A strike's contact time before Variation and the 20 ms cap: Hardness, Attack's softening and the Hertz law.
	[[nodiscard]] static double strikeContactSeconds(double hardness, double attack, double velocity) noexcept;
	static constexpr double maximumContactSeconds = 0.02;
	[[nodiscard]] static double dampingExponent(double material) noexcept;

	// Voicing constants (docs/FLINT_VALIDATION.md, Bar sheet).
	static constexpr double evenModeRadiation = 0.5; // antisymmetric modes radiate less: -6 dB
	static constexpr double tubeQ = 30.0;
	static constexpr double noiseHighPassHz = 1'000.0;
	static constexpr double softNoiseDecibels = -22.0; // re the body's nominal peak, Hardness 0
	static constexpr double hardNoiseDecibels = -10.0; // Hardness 100 %
	static constexpr double dampedT60Seconds = 0.08;
	static constexpr double decayPitchExponent = 0.5; // T60 scales with (f / 261.6 Hz)^-0.5
	static constexpr double nominalPeak = 0.5;
	// A full-velocity default hit at C4 peaks at nominalPeak (calibrated 3 October 2026).
	static constexpr double barLevel = 1.025;

private:
	void setTargets(const FlintParameters& parameters) noexcept;
	void updateModes(int advance) noexcept;
	[[nodiscard]] double pulseAt(double seconds) const noexcept;
	[[nodiscard]] double predictedPeak(double shape, double point) const noexcept;
	static constexpr double contactShapes = 10.0; // the pulse lasts 10 theta (0.05 % of its peak remains)

	double sampleRateHz { 44'100.0 };
	ModalBank bar;
	ModalBank tube;
	dsp::LinearTptSvf noiseFilter;
	HitNoise contactNoise;

	dsp::LinearRamp pitchNote, decay, tone, material, overtones, resonator, damping;
	double attack { 0.3 };
	double hardness { 0.5 };
	double position { 0.25 };
	double variation {};
	bool noteOffDamps {};

	std::array<double, barModes> modeFrequency {};
	std::array<bool, barModes> wasAudible {};
	std::array<double, barModes> gainFrom {}, gainTo {}; // Tone's tilt, interpolated between updates
	int samplesUntilUpdate {};
	int updateSample {};
	double fundamentalHz { 261.6 };
	double noiseTilt { 1.0 };

	// The current hit.
	double pulseShape {}; // theta of the force t e^{-t / theta}, seconds
	double pulseLength {}; // seconds
	double pulseAmplitude {};
	double pulsePeak {};
	double elapsedSeconds {};
	double noiseLevel {};
	bool striking {};
	bool noiseRinging {};
};
}
