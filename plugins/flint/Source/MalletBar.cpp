#include "MalletBar.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace vekt::flint
{
namespace
{
constexpr double rampSeconds = 0.015;
constexpr double dampingRampSeconds = 0.002;
constexpr int updateInterval = 32; // samples between mode updates; gains are interpolated in between
constexpr double noiseTailSeconds = 0.005;
constexpr double sixtyDecibelsNeper = 6.907755278982137;
constexpr double shortestT60 = 0.002;

// The free-free beam's eigenvalues: beta_n L, from cosh(beta) cos(beta) = 1.
constexpr std::array<double, MalletBar::barModes> beamEigenvalues { 4.730040745, 7.853204624, 10.995607838,
	14.137165491, 17.278759657, 20.420352251, 23.561944902, 26.703537555, 29.845130209, 32.986722863, 36.128315516,
	39.269908170 };

[[nodiscard]] double logLerp(double from, double to, double amount) noexcept
{
	return std::exp(std::log(from) + amount * (std::log(to) - std::log(from)));
}
}

double MalletBar::pitchHz(double note) noexcept
{
	constexpr auto range = pitchRangeOf(ModelId::malletBar);
	return 440.0 * std::exp2((std::clamp(note, range.lowest, range.highest) - 69.0) / 12.0);
}

std::array<double, MalletBar::barModes> MalletBar::ratios(double overtonesAmount) noexcept
{
	std::array<double, barModes> result {};
	for (std::size_t mode = 0; mode < barModes; ++mode)
	{
		const auto ratio = beamEigenvalues[mode] / beamEigenvalues[0];
		result[mode] = ratio * ratio;
	}
	// Modes 2 and 3: uniform -> xylophone (3, 6) at 50 % -> marimba (4, 10) at 100 %, in log-frequency.
	const auto amount = std::clamp(overtonesAmount, 0.0, 1.0);
	const auto uniformThird = result[2];
	if (amount <= 0.5)
	{
		result[1] = logLerp(result[1], 3.0, 2.0 * amount);
		result[2] = logLerp(result[2], 6.0, 2.0 * amount);
	}
	else
	{
		result[1] = logLerp(3.0, 4.0, 2.0 * amount - 1.0);
		result[2] = logLerp(6.0, 10.0, 2.0 * amount - 1.0);
	}
	for (std::size_t mode = 3; mode < barModes; ++mode) result[mode] *= result[2] / uniformThird;
	return result;
}

double MalletBar::modeShape(std::size_t mode, double point) noexcept
{
	const auto beta = beamEigenvalues[mode];
	const auto sigma = (std::cosh(beta) - std::cos(beta)) / (std::sinh(beta) - std::sin(beta));
	const auto x = beta * point;
	return 0.5 * (std::cosh(x) + std::cos(x) - sigma * (std::sinh(x) + std::sin(x)));
}

double MalletBar::strikePoint(double positionAmount) noexcept
{
	return 0.5 - 0.47 * std::clamp(positionAmount, 0.0, 1.0);
}

double MalletBar::fundamentalT60(double decayAmount, double frequencyHz) noexcept
{
	return 0.05 * std::pow(400.0, decayAmount) * std::pow(frequencyHz / 261.6255653005986, -decayPitchExponent);
}

double MalletBar::contactSeconds(double hardnessAmount) noexcept
{
	return 0.006 * std::pow(1.0 / 24.0, hardnessAmount);
}

double MalletBar::strikeContactSeconds(double hardnessAmount, double attackAmount, double velocity) noexcept
{
	return contactSeconds(hardnessAmount) * std::pow(velocity, -0.2) * (1.0 + 4.0 * attackAmount * attackAmount);
}

double MalletBar::dampingExponent(double materialAmount) noexcept { return 1.8 - 1.4 * materialAmount; }

void MalletBar::prepare(double sampleRate, int) noexcept
{
	sampleRateHz = sampleRate;
	bar.prepare(sampleRate);
	bar.setModeCount(barModes);
	tube.prepare(sampleRate);
	tube.setModeCount(1);
	tube.setOutputGain(0, 1.0);
	noiseFilter.prepare(sampleRate);
	contactNoise.prepare(sampleRate);
	for (auto* ramp : { &pitchNote, &decay, &tone, &material, &overtones, &resonator })
		ramp->reset(sampleRate, rampSeconds);
	damping.reset(sampleRate, dampingRampSeconds);
	reset();
}

void MalletBar::reset() noexcept
{
	bar.reset();
	tube.reset();
	noiseFilter.reset();
	damping.setCurrentAndTargetValue(0.0);
	striking = false;
	noiseRinging = false;
}

void MalletBar::setTargets(const FlintParameters& parameters) noexcept
{
	pitchNote.setTargetValue(parameters.pitch);
	decay.setTargetValue(parameters.decay);
	tone.setTargetValue(parameters.tone);
	material.setTargetValue(parameters.malletBar.material);
	overtones.setTargetValue(parameters.malletBar.overtones);
	resonator.setTargetValue(parameters.malletBar.resonator);
	attack = parameters.attack;
	hardness = parameters.malletBar.hardness;
	position = parameters.malletBar.position;
	variation = parameters.variation;
	noteOffDamps = parameters.noteOffDamps;
}

void MalletBar::activate(const FlintParameters& parameters) noexcept
{
	setTargets(parameters);
	for (auto* ramp : { &pitchNote, &decay, &tone, &material, &overtones, &resonator })
		ramp->setCurrentAndTargetValue(ramp->getTargetValue());
	// A released note's damping ends with it: the next hit on the silent object starts undamped (A16 a).
	damping.setCurrentAndTargetValue(0.0);
	updateModes(0);
	gainFrom = gainTo;
	samplesUntilUpdate = updateInterval;
	updateSample = 0;
}

void MalletBar::update(const FlintParameters& parameters) noexcept { setTargets(parameters); }

void MalletBar::updateModes(int advance) noexcept
{
	const auto note = pitchNote.skip(advance);
	const auto decayAmount = decay.skip(advance);
	const auto toneAmount = tone.skip(advance);
	const auto materialAmount = material.skip(advance);
	const auto overtonesAmount = overtones.skip(advance);
	const auto damped = damping.skip(advance);

	fundamentalHz = pitchHz(note);
	const auto modeRatios = ratios(overtonesAmount);
	const auto exponent = dampingExponent(materialAmount);
	const auto fundamentalDecay = fundamentalT60(decayAmount, fundamentalHz);
	const auto tiltDecibelsPerOctave = 24.0 * (toneAmount - 0.5);
	gainFrom = gainTo;
	for (std::size_t mode = 0; mode < barModes; ++mode)
	{
		auto t60 = std::max(fundamentalDecay * std::pow(modeRatios[mode], -exponent), shortestT60);
		if (damped > 0.0 && t60 > dampedT60Seconds) t60 = logLerp(t60, dampedT60Seconds, damped);
		modeFrequency[mode] = fundamentalHz * modeRatios[mode];
		const auto limit = std::min(ModalBank::frequencyCeilingHz, 0.5 * sampleRateHz);
		const auto above = modeFrequency[mode] >= limit;
		if (above && wasAudible[mode])
		{
			// Crossing the ceiling (a glide, Overtones): fade the mode out over this update, then drop it.
			bar.setMode(mode, 0.999 * limit, t60);
			gainTo[mode] = 0.0;
		}
		else
		{
			bar.setMode(mode, modeFrequency[mode], t60);
			gainTo[mode] = above ? 0.0 : std::pow(10.0, tiltDecibelsPerOctave * std::log2(modeRatios[mode]) / 20.0);
		}
		wasAudible[mode] = !above;
	}
	const auto tubeT60 = sixtyDecibelsNeper * tubeQ / (std::numbers::pi * fundamentalHz);
	tube.setMode(0, fundamentalHz, tubeT60);
	// Unity gain at resonance for a real input: half of it drives the positive-frequency pole.
	tube.setInputGain(0, 2.0 * (1.0 - std::abs(tube.pole(0))));
	noiseTilt = std::pow(10.0, tiltDecibelsPerOctave * std::log2(4'000.0 / fundamentalHz) / 20.0);
}

double MalletBar::pulseAt(double seconds) const noexcept
{
	if (seconds > pulseLength) return 0.0;
	return pulseAmplitude * seconds * std::exp(-seconds / pulseShape);
}

double MalletBar::predictedPeak(double shape, double point) const noexcept
{
	// The strike's output from rest, over the pulse and one fundamental period, at 48 kHz or 8 samples per theta if that
	// is finer, but never finer than the engine itself samples the pulse: cheap at any internal rate, and true to what
	// the engine renders. Each mode decays as the bar's does: a wooden bar's high modes lose 60 dB within milliseconds.
	const auto rate = std::min(sampleRateHz, std::max(48'000.0, 8.0 / shape));
	std::array<std::complex<double>, barModes> rotation {}, state {};
	std::array<double, barModes> weight {};
	for (std::size_t mode = 0; mode < barModes; ++mode)
	{
		if (modeFrequency[mode] >= ModalBank::frequencyCeilingHz) continue;
		const auto radius = std::pow(std::abs(bar.pole(mode)), sampleRateHz / rate);
		rotation[mode] = std::polar(radius, 2.0 * std::numbers::pi * modeFrequency[mode] / rate);
		weight[mode] = modeShape(mode, point) * (mode % 2 == 1 ? evenModeRadiation : 1.0) * gainTo[mode];
	}
	const auto length = static_cast<int>(std::ceil((contactShapes * shape + 1.0 / fundamentalHz) * rate));
	auto peak = 0.0;
	for (auto index = 0; index < length; ++index)
	{
		const auto time = static_cast<double>(index) / rate;
		const auto input =
		    time <= contactShapes * shape ? time * std::exp(-time / shape) / (shape * shape * rate) : 0.0;
		auto output = 0.0;
		for (std::size_t mode = 0; mode < barModes; ++mode)
		{
			state[mode] = rotation[mode] * state[mode] + input;
			output += weight[mode] * state[mode].imag();
		}
		peak = std::max(peak, std::abs(output));
	}
	return peak;
}

void MalletBar::trigger(Strike strike) noexcept
{
	// Draw order (Bar sheet): position, contact time, force, contact-noise level, contact-noise seed.
	HitRandom random(strike.hash);
	const auto positionOffset = 0.04 * variation * random.bell();
	const auto contactScale = 1.0 + 0.1 * variation * random.bell();
	const auto forceDecibels = variation * random.bell();
	const auto noiseDecibels = 3.0 * variation * random.bell();
	contactNoise.start(random.seed(), variation);

	// Hertz contact: a stronger strike is shorter (tau ~ v^-1/5); the impulse is the mallet's momentum (~ v). The force
	// rises and falls as t e^{-t / theta}, theta = tau / 7: no spectral nulls, and a 1 / f^2 roll-off like a real contact.
	const auto velocity = std::clamp(strike.velocity, 0.01, 1.0);
	const auto contact =
	    std::min(maximumContactSeconds, strikeContactSeconds(hardness, attack, velocity) * contactScale);
	pulseShape = contact / 7.0;
	pulseLength = contactShapes * pulseShape;
	const auto point = std::clamp(strikePoint(position) + positionOffset, 0.0, 0.5);
	// Attack and Hardness change the brightness, not the level: the strike peaks as the default contact would at this
	// velocity.
	const auto referenceShape = contactSeconds(0.5) * std::pow(velocity, -0.2) * (1.0 + 4.0 * 0.3 * 0.3) / 7.0;
	const auto actualPeak = predictedPeak(pulseShape, point);
	const auto contactLevel = actualPeak > 0.0 ? predictedPeak(referenceShape, point) / actualPeak : 1.0;
	const auto force = velocity * std::pow(10.0, forceDecibels / 20.0) * barLevel * contactLevel;
	// Per sample: the pulse's samples sum to `force`, whatever the sample rate.
	pulseAmplitude = force / (pulseShape * pulseShape * sampleRateHz);
	pulsePeak = pulseAmplitude * pulseShape * std::exp(-1.0);

	const auto lengthSamples = std::ceil(pulseLength * sampleRateHz);
	const auto decayPerSample = std::exp(-1.0 / (pulseShape * sampleRateHz));
	for (std::size_t mode = 0; mode < barModes; ++mode)
	{
		const auto weight = modeShape(mode, point) * (mode % 2 == 1 ? evenModeRadiation : 1.0);
		const auto pole = bar.pole(mode);
		if (std::norm(pole) <= 0.0) continue;
		// Exact for the sampled pulse: sum_k (A / fs) k a^k = (A / fs) a / (1 - a)^2 with a = e^{-1 / (theta fs)} / p.
		// Exact for the sampled pulse over its N samples: sum_k (A / fs) k a^k = (A / fs) a (1 - (N + 1) a^N + N a^(N+1)) /
		// (1 - a)^2, with a = e^{-1 / (theta fs)} / p (|a| may exceed 1 when the mode decays faster than the pulse).
		const auto ratio = decayPerSample / pole;
		const auto power = std::pow(ratio, lengthSamples);
		const auto pulseSum = pulseAmplitude / sampleRateHz * ratio *
		    (1.0 - (lengthSamples + 1.0) * power + lengthSamples * power * ratio) / ((1.0 - ratio) * (1.0 - ratio));
		const auto end = std::pow(pole, lengthSamples);
		// Each mode may build up to twice a strike, so the bar's energy stays within +6 dB of one strike.
		bar.setInputGain(mode, weight * strikeScale(bar.state(mode) * end, weight * end * pulseSum));
	}

	const auto noiseDecibelsAtHardness = softNoiseDecibels + (hardNoiseDecibels - softNoiseDecibels) * hardness;
	// Impact noise grows faster with velocity than the tone does (v^1.5), so harder hits sound brighter; its energy does
	// not grow with the contact's duration (a soft, long contact makes less noise, not more).
	noiseLevel = nominalPeak * std::pow(10.0, (noiseDecibelsAtHardness + noiseDecibels) / 20.0) * noiseTilt *
	    std::pow(velocity, 1.5) * std::sqrt(referenceShape / pulseShape);
	noiseFilter.reset();
	noiseRinging = true;
	elapsedSeconds = 0.0;
	striking = true;
	damping.setTargetValue(0.0);
}

void MalletBar::release(int) noexcept
{
	if (noteOffDamps) damping.setTargetValue(1.0);
}

void MalletBar::process(std::span<float> left, std::span<float> right) noexcept
{
	for (std::size_t index = 0; index < left.size(); ++index)
	{
		if (samplesUntilUpdate <= 0)
		{
			updateModes(updateInterval);
			samplesUntilUpdate = updateInterval;
			updateSample = 0;
		}
		const auto fraction = static_cast<double>(updateSample) / static_cast<double>(updateInterval);
		for (std::size_t mode = 0; mode < barModes; ++mode)
			bar.setOutputGain(mode, gainFrom[mode] + (gainTo[mode] - gainFrom[mode]) * fraction);
		--samplesUntilUpdate;
		++updateSample;

		const auto pulse = striking ? pulseAt(elapsedSeconds) : 0.0;
		const auto body = bar.process(pulse);
		auto output = body;
		// The tube's level moves per sample; once it reaches 0 the tube is cleared, so no stale ring returns later.
		if (const auto tubeLevel = resonator.getNextValue(); tubeLevel > 0.0)
			output += tubeLevel * tube.process(body);
		else if (tube.isActive())
			tube.reset();
		if (noiseRinging)
		{
			const auto envelope = pulsePeak > 0.0 ? pulse / pulsePeak : 0.0;
			const auto noise = contactNoise.next() * envelope * noiseLevel;
			output += noiseFilter.process(noise, noiseHighPassHz, std::numbers::sqrt2).highPass;
		}
		left[index] = static_cast<float>(output);
		right[index] = static_cast<float>(output);

		elapsedSeconds += 1.0 / sampleRateHz;
		if (striking && elapsedSeconds > pulseLength) striking = false;
		if (noiseRinging && elapsedSeconds > pulseLength + noiseTailSeconds)
		{
			noiseRinging = false;
			noiseFilter.reset();
		}
	}
}

bool MalletBar::isActive() const noexcept
{
	return striking || noiseRinging || bar.isActive() || (resonator.getCurrentValue() > 0.0 && tube.isActive());
}

double MalletBar::energy() const noexcept { return bar.energy(); }
}
