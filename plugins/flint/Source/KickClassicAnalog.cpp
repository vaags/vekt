#include "KickClassicAnalog.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace vekt::flint
{
namespace
{
constexpr double rampSeconds = 0.015;
constexpr double dampingRampSeconds = 0.002;
constexpr double circuitPulseFallSeconds = 0.0003; // Attack 30 %: the pulse shaper's own settling
constexpr double clickTailSeconds = 0.005;
constexpr double sixtyDecibelsNeper = 6.907755278982137;
constexpr double silence = 1.0e-9;

[[nodiscard]] double accentVolts(double velocity) noexcept { return 4.0 + 10.0 * std::clamp(velocity, 0.0, 1.0); }

}

double KickClassicAnalog::pitchHz(double note) noexcept
{
	constexpr auto range = pitchRangeOf(ModelId::kickClassicAnalog);
	return 440.0 * std::exp2((std::clamp(note, range.lowest, range.highest) - 69.0) / 12.0);
}

double KickClassicAnalog::t60Seconds(double decayAmount) noexcept { return 0.04 * std::pow(200.0, decayAmount); }

double KickClassicAnalog::toneCutoffHz(double toneAmount) noexcept { return 150.0 * std::pow(133.0, toneAmount); }

double KickClassicAnalog::pulseFallSeconds(double attackAmount) noexcept
{
	// 0 % 0.1 ms, 30 % the circuit's own pulse, 100 % 2 ms, exponential in each segment.
	if (attackAmount <= 0.3) return 0.0001 * std::pow(circuitPulseFallSeconds / 0.0001, attackAmount / 0.3);
	return circuitPulseFallSeconds * std::pow(0.002 / circuitPulseFallSeconds, (attackAmount - 0.3) / 0.7);
}

double KickClassicAnalog::sweepSemitones(double sweepAmount) noexcept { return 36.0 * sweepAmount * sweepAmount; }

double KickClassicAnalog::sweepTimeSeconds(double sweepTimeAmount) noexcept
{
	return 0.002 * std::pow(250.0, sweepTimeAmount);
}

void KickClassicAnalog::prepare(double sampleRate, int) noexcept
{
	sampleRateHz = sampleRate;
	resonance.prepare(sampleRate);
	resonance.setModeCount(1);
	resonance.setOutputGain(0, 1.0);
	clickFilter.prepare(sampleRate);
	beaterNoise.prepare(sampleRate);
	for (auto* ramp : { &pitchNote, &decay, &tone, &bodyShape }) ramp->reset(sampleRate, rampSeconds);
	damping.reset(sampleRate, dampingRampSeconds);
	attackWindowSamples = std::round(attackWindowSeconds * sampleRate);
	reset();
}

void KickClassicAnalog::reset() noexcept
{
	resonance.reset();
	clickFilter.reset();
	damping.setCurrentAndTargetValue(0.0);
	striking = false;
	clickRinging = false;
	previousPulse = 0.0;
	sweepEnvelope = 0.0;
	toneState = 0.0;
}

void KickClassicAnalog::setTargets(const FlintParameters& parameters) noexcept
{
	pitchNote.setTargetValue(parameters.pitch);
	decay.setTargetValue(parameters.decay);
	tone.setTargetValue(parameters.tone);
	bodyShape.setTargetValue(parameters.kickClassicAnalog.bodyShape);
	attack = parameters.attack;
	sweep = parameters.kickClassicAnalog.sweep;
	sweepTime = parameters.kickClassicAnalog.sweepTime;
	clickAmount = parameters.kickClassicAnalog.click;
	variation = parameters.variation;
	noteOffDamps = parameters.noteOffDamps;
}

void KickClassicAnalog::activate(const FlintParameters& parameters) noexcept
{
	setTargets(parameters);
	for (auto* ramp : { &pitchNote, &decay, &tone, &bodyShape }) ramp->setCurrentAndTargetValue(ramp->getTargetValue());
	// A released note's damping ends with it: the next hit on the silent object starts undamped (A16 a).
	damping.setCurrentAndTargetValue(0.0);
}

void KickClassicAnalog::update(const FlintParameters& parameters) noexcept { setTargets(parameters); }

double KickClassicAnalog::pulseAt(const PulseShape& shape, double seconds) noexcept
{
	if (seconds < gateSeconds)
		return shape.rise * ((1.0 - shelfRatio) * std::exp(-seconds / shape.fallSeconds) + shelfRatio);
	return -shape.fall * std::exp(-(seconds - gateSeconds) / shape.fallSeconds);
}

double KickClassicAnalog::pulseSeconds(const PulseShape& shape) noexcept
{
	return gateSeconds + 8.0 * shape.fallSeconds;
}

KickClassicAnalog::Response KickClassicAnalog::responseTo(
    const PulseShape& shape, const Prediction& path) const noexcept
{
	// At the engine's rate or 96 kHz, whichever is lower: the differenced input makes the response's evolution in time
	// independent of the rate, so a strike costs the same at every quality.
	const auto rate = std::min(sampleRateHz, predictionRate);
	const auto length = static_cast<int>(std::ceil(pulseSeconds(shape) * rate));
	const auto period = static_cast<int>(std::ceil(rate / path.settledHz));
	const auto radius = std::exp(-sixtyDecibelsNeper / (path.t60 * rate));
	Response response;
	std::complex<double> state;
	auto previous = 0.0;
	for (auto index = 0; index <= length + period; ++index)
	{
		const auto seconds = static_cast<double>(index) / rate;
		auto frequency =
		    path.settledHz * std::exp2(path.sweepSemitones * std::exp(-seconds / path.sweepSeconds) / 12.0);
		if (seconds < attackWindowSeconds) frequency *= attackShift;
		const auto pole = std::polar(radius, 2.0 * std::numbers::pi * frequency / rate);
		const auto value = index <= length ? pulseAt(shape, seconds) : 0.0;
		state = pole * state + (value - previous);
		previous = value;
		response.peak = std::max(response.peak, std::abs(state.imag()));
		if (index <= length) response.rotation *= pole;
		if (index == length) response.state = state;
	}
	return response;
}

void KickClassicAnalog::trigger(Strike strike) noexcept
{
	// Draw order (Kick sheet): click level, click cutoff, sweep depth, sweep time, beater-noise seed.
	HitRandom random(strike.hash);
	const auto clickDecibels = 3.0 * variation * random.bell();
	const auto clickSemitones = 4.0 * variation * random.bell();
	const auto sweepDepthScale = 1.0 + 0.1 * variation * random.bell();
	const auto sweepTimeScale = 1.0 + 0.1 * variation * random.bell();
	beaterNoise.start(random.seed(), variation);

	const auto volts = accentVolts(strike.velocity);
	pulse = { volts / maximumAccentVolts, diodeVolts / maximumAccentVolts, pulseFallSeconds(attack) };
	const PulseShape circuitPulse { pulse.rise, pulse.fall, circuitPulseFallSeconds };
	const PulseShape fullAccentPulse { 1.0, pulse.fall, circuitPulseFallSeconds };
	sweepDepth = sweepSemitones(sweep) * (0.6 + 0.4 * strike.velocity) * sweepDepthScale;
	const auto sweepSeconds = sweepTimeSeconds(sweepTime) * sweepTimeScale;

	// Predicted along the path the ringing resonance will take: its current sigh, the attack shift and the sweep.
	const Prediction path { pitchHz(pitchNote.getCurrentValue()) * sigh(), t60Seconds(decay.getCurrentValue()),
		sweepDepth, sweepSeconds };
	// The resonance's level follows the accent, is the same for every Attack (it peaks as after the circuit's own
	// pulse), and is nominal at full accent.
	const auto response = responseTo(pulse, path);
	const auto attackIndependent = responseTo(circuitPulse, path).peak / response.peak;
	const auto normalized = bodyLevel / std::abs(responseTo(fullAccentPulse, path).state);
	const auto cap =
	    strikeScale(resonance.state(0) * response.rotation, normalized * attackIndependent * response.state);
	resonance.setInputGain(0, normalized * attackIndependent * cap);
	strikeLevel = attackIndependent * cap;
	clickPulseLevel = attackIndependent;

	pulseLengthSamples = std::ceil(pulseSeconds(pulse) * sampleRateHz);
	elapsedSamples = 0.0;
	sweepDecayPerSample = std::exp(-1.0 / (sweepSeconds * sampleRateHz));
	sweepEnvelope = 1.0;
	clickLevel = clickAmount * nominalPeak * strike.velocity * strike.velocity * std::pow(10.0, clickDecibels / 20.0);
	clickCutoff = clickCutoffHz * std::exp2(clickSemitones / 12.0);
	clickRinging = clickLevel > 0.0;
	if (clickRinging) clickFilter.reset();
	damping.setTargetValue(0.0);
	striking = true;
}

void KickClassicAnalog::release(int) noexcept
{
	if (noteOffDamps) damping.setTargetValue(1.0);
}

void KickClassicAnalog::process(std::span<float> left, std::span<float> right) noexcept
{
	const auto toneLimit = 0.45 * sampleRateHz;
	for (std::size_t index = 0; index < left.size(); ++index)
	{
		auto frequency = pitchHz(pitchNote.getNextValue());
		auto t60 = t60Seconds(decay.getNextValue());
		if (const auto damped = damping.getNextValue(); damped > 0.0 && t60 > dampedT60Seconds)
			t60 = std::exp((1.0 - damped) * std::log(t60) + damped * std::log(dampedT60Seconds));

		auto pulseValue = 0.0;
		if (striking)
		{
			if (sweepDepth > 0.0)
			{
				frequency *= std::exp2(sweepDepth * sweepEnvelope / 12.0);
				sweepEnvelope *= sweepDecayPerSample;
			}
			if (elapsedSamples < attackWindowSamples) frequency *= attackShift;
			if (elapsedSamples <= pulseLengthSamples) pulseValue = pulseAt(pulse, elapsedSamples / sampleRateHz);
		}
		frequency *= sigh();
		resonance.setMode(0, frequency, t60);

		const auto input = pulseValue - previousPulse;
		previousPulse = pulseValue;
		auto output = resonance.process(input);
		if (const auto shape = bodyShape.getNextValue(); shape > 0.0)
		{
			const auto gain = 5.0 * shape * shape;
			output = nominalPeak * std::tanh(gain * output / nominalPeak) / std::tanh(gain);
		}
		output += directGain * strikeLevel * pulseValue;
		if (clickRinging)
		{
			// The beater's contact noise outlasts the 1 ms electrical pulse: it decays from the strike over a few ms.
			const auto noiseEnvelope = pulse.rise * std::exp(-elapsedSamples / (beaterNoiseSeconds * sampleRateHz));
			const auto beater = clickPulseLevel * (pulseValue + beaterNoiseLevel * noiseEnvelope * beaterNoise.next());
			output += clickLevel * clickFilter.process(beater, clickCutoff, std::numbers::sqrt2).highPass;
		}

		const auto cutoff = std::min(toneCutoffHz(tone.getNextValue()), toneLimit);
		const auto gain = std::tan(std::numbers::pi * cutoff / sampleRateHz);
		const auto step = (output - toneState) * gain / (1.0 + gain);
		const auto toned = step + toneState;
		toneState = toned + step;

		left[index] = static_cast<float>(toned);
		right[index] = static_cast<float>(toned);

		if (striking)
		{
			elapsedSamples += 1.0;
			if (clickRinging && elapsedSamples > pulseLengthSamples + clickTailSeconds * sampleRateHz)
			{
				clickRinging = false;
				clickFilter.reset();
			}
			// Done once past the pulse and attack window, with the click over and the sweep either below a tenth of a
			// cent or with nothing left ringing for it to move.
			if (elapsedSamples > std::max(pulseLengthSamples, attackWindowSamples) && !clickRinging &&
			    (sweepEnvelope * sweepDepth < 1.0e-3 || !resonance.isActive()))
				striking = false;
		}
	}
	if (!striking && !resonance.isActive() && std::abs(toneState) < silence)
	{
		toneState = 0.0;
		previousPulse = 0.0;
	}
}

double KickClassicAnalog::sigh() const noexcept
{
	// The pitch sigh: leakage lifts the frequency while the resonance swings wide.
	const auto amplitude = std::abs(resonance.state(0)) / bodyLevel;
	return 1.0 + sighDepth * std::min(amplitude * amplitude, 4.0);
}

bool KickClassicAnalog::isActive() const noexcept
{
	return striking || resonance.isActive() || std::abs(toneState) >= silence;
}

double KickClassicAnalog::energy() const noexcept { return resonance.energy(); }
}
