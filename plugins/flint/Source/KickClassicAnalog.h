#pragma once

#include "FlintEngine.h"
#include "HitRandom.h"
#include "ModalBank.h"

#include <vekt/dsp/LinearRamp.h>
#include <vekt/dsp/LinearTptSvf.h>

#include <complex>

namespace vekt::flint
{
// Kick / Classic Analog (docs/FLINT_VALIDATION.md, Kick sheet): the 808-lineage bass drum after Werner, Abel and Smith
// (DAFx 2014). The bridged-T network's transfer function is H(s) = 1 + b s / (a2 s^2 + a1 s + 1): the shaped trigger
// pulse passes straight through (the circuit's own click) and drives a band-pass resonance, here one ModalBank mode
// with differenced input. During the first 6 ms the resonance sits more than an octave higher; afterwards its pitch
// "sighs" down as its amplitude falls; a first-order low-pass is the tone stage. The paper gives the structure but not
// the component values (they are in Roland's service notes), so the numbers it leaves open are named voicing
// constants. Sweep, Click and Body Shape are extensions, each skipped at 0.
class KickClassicAnalog final : public FlintEngine
{
public:
	void prepare(double sampleRate, int maximumBlockSize) noexcept override;
	void reset() noexcept override;
	void activate(const FlintParameters& parameters) noexcept override;
	void update(const FlintParameters& parameters) noexcept override;
	void trigger(Strike strike) noexcept override;
	void release(int note) noexcept override;
	void process(std::span<float> left, std::span<float> right) noexcept override;
	[[nodiscard]] bool isActive() const noexcept override;
	[[nodiscard]] double energy() const noexcept override;

	// The sheet's mappings.
	[[nodiscard]] static double pitchHz(double note) noexcept; // clamped to B0–C5
	[[nodiscard]] static double t60Seconds(double decay) noexcept;
	[[nodiscard]] static double toneCutoffHz(double tone) noexcept;
	[[nodiscard]] static double pulseFallSeconds(double attack) noexcept;
	[[nodiscard]] static double sweepSemitones(double sweep) noexcept;
	[[nodiscard]] static double sweepTimeSeconds(double sweepTime) noexcept;

	// Voicing constants (docs/FLINT_VALIDATION.md, Kick sheet).
	static constexpr double gateSeconds = 0.001; // the trigger logic's 1 ms pulse
	static constexpr double shelfRatio = 0.1; // the pulse shaper's plateau, R162 / (R162 + R163)
	static constexpr double diodeVolts = 0.71; // the falling edge's clamp
	static constexpr double maximumAccentVolts = 14.0; // trigger amplitude range 4–14 V
	static constexpr double attackWindowSeconds = 0.006; // the envelope generator holds the shift ~6 ms
	static constexpr double attackShift = 2.3; // "more than an octave"
	static constexpr double sighDepth = 0.14; // frequency lift at nominal amplitude (Fig. 11: 48–58 Hz)
	static constexpr double nominalPeak = 0.5; // -6 dBFS
	// The resonance's amplitude for a full-accent hit, set so that hit (with the attack shift and the direct pulse)
	// peaks at nominalPeak after the default tone stage (calibrated 3 October 2026).
	static constexpr double bodyLevel = 0.356;
	static constexpr double directGain = 0.2; // the pulse through H's direct path, re the nominal body peak
	static constexpr double clickCutoffHz = 1'500.0;
	// The beater noise re the pulse: +3.5 dB, so each hit's click has its own texture (hit-to-hit correlation of the click
	// band below 0.9 at full Variation, A16).
	static constexpr double beaterNoiseLevel = 1.5;
	static constexpr double beaterNoiseSeconds = 0.002; // the beater noise's decay time constant
	static constexpr double dampedT60Seconds = 0.06;

private:
	struct PulseShape
	{
		double rise {}; // rising-edge height, accent-dependent
		double fall {}; // falling-edge height, clamped by the diode
		double fallSeconds {};
	};
	[[nodiscard]] static double pulseAt(const PulseShape& shape, double seconds) noexcept;
	[[nodiscard]] static double pulseSeconds(const PulseShape& shape) noexcept;
	struct Response
	{
		std::complex<double> state; // when the pulse ends
		double peak {}; // the largest output, until one settled period after the pulse
		std::complex<double> rotation { 1.0, 0.0 }; // what the pulse's duration does to a state already ringing
	};
	// The pitch path a strike will follow.
	struct Prediction
	{
		double settledHz {};
		double t60 {};
		double sweepSemitones {};
		double sweepSeconds { 1.0 };
	};
	static constexpr double predictionRate = 96'000.0;
	// The resonance's response to one pulse from rest, for input gain 1, along the strike's pitch path.
	[[nodiscard]] Response responseTo(const PulseShape& shape, const Prediction& path) const noexcept;
	void setTargets(const FlintParameters& parameters) noexcept;
	[[nodiscard]] double sigh() const noexcept;

	double sampleRateHz { 44'100.0 };
	ModalBank resonance;
	dsp::LinearTptSvf clickFilter;
	HitNoise beaterNoise;

	dsp::LinearRamp pitchNote, decay, tone, bodyShape, damping;
	double attack { 0.3 };
	double clickAmount {};
	double sweep {};
	double sweepTime { 0.3 };
	double variation {};
	bool noteOffDamps {};

	// The current hit.
	PulseShape pulse;
	double strikeLevel {}; // the pulse's level: Attack-independent, after the cap
	double clickPulseLevel {}; // the pulse's level for the click: Attack-independent, without the cap
	double elapsedSamples {}; // since the strike
	double pulseLengthSamples {};
	double attackWindowSamples {};
	double sweepDepth {}; // semitones, latched at the strike
	double sweepDecayPerSample { 1.0 };
	double sweepEnvelope {};
	double clickLevel {}; // latched at the strike: Click, velocity and Variation
	double clickCutoff { clickCutoffHz };
	double previousPulse {};
	double toneState {};
	bool striking {};
	bool clickRinging {};
};
}
