#include "RotaryEngine.h"

#include <cmath>
#include <numbers>

namespace vekt::glimmer
{
int RotaryEngine::latencySamples(double rate) noexcept { return static_cast<int>(std::lround(0.008 * rate)); }
int RotaryEngine::capacitySamples(double rate) noexcept { return static_cast<int>(std::ceil(0.012 * rate)) + 4; }

float RotaryEngine::pickupDelay(double rate, float micDistance, float radius, float facing) noexcept
{
	const auto boundedDistance = std::max(0.3f, micDistance);
	const auto path = std::sqrt(boundedDistance * boundedDistance + radius * radius
		- 2.0f * boundedDistance * radius * std::clamp(facing, -1.0f, 1.0f));
	return static_cast<float>(latencySamples(rate)) + (path - boundedDistance) * static_cast<float>(rate / 343.0);
}

std::array<float, 2> RotaryEngine::applyWidth(std::array<float, 2> input, float width) noexcept
{
	const auto mid = (input[0] + input[1]) * 0.5f;
	const auto side = (input[0] - input[1]) * 0.5f * std::clamp(width, 0.0f, 2.0f);
	return { mid + side, mid - side };
}

RotaryEngine::Profile RotaryEngine::profileFor(CabinetModel selected) noexcept
{
	switch (selected)
	{
	case CabinetModel::drum: return { 800, 1, 1, 1, 1, 0.19f, 0.12f, 0.18f, 4200, 0.8f, 0, 0.48f };
	case CabinetModel::wide: return { 950, 0.78f, 0.9f, 1.7f, 1.9f, 0.34f, 0.16f, 0.2f, 12000, 0.65f, 0.75f, 0.32f };
	case CabinetModel::classic:
	default: return { 800, 0.83f, 0.86f, 1.6f, 1.9f, 0.25f, 0.12f, 0.18f, 6500, 0.75f, 0.65f, 0.3f };
	}
}

float RotaryEngine::coefficient(float frequency) const noexcept
{
	return 1.0f - std::exp(-2.0f * std::numbers::pi_v<float>
		* std::clamp(frequency, 10.0f, static_cast<float>(sampleRate * 0.45)) / static_cast<float>(sampleRate));
}

void RotaryEngine::prepare(double rate, int maximumBlockSize)
{
	sampleRate = rate;
	const juce::dsp::ProcessSpec spec { rate, static_cast<juce::uint32>(maximumBlockSize), 2 };
	crossover.prepare(spec);
	cabinet.prepare(spec);
	cabinet.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
	for (auto* delays : { &hornDelay, &drumDelay })
		for (auto& delay : *delays)
		{
			delay.setMaximumDelayInSamples(capacitySamples(rate));
			delay.prepare({ rate, static_cast<juce::uint32>(maximumBlockSize), 1 });
		}
	for (auto* smoother : { &balance, &hornTone, &drumTone }) smoother->prepare(rate, 0.02);
	for (auto* smoother : { &angle, &distance }) smoother->prepare(rate, 0.05);
	hornMotion = RotorMotion {};
	drumMotion = RotorMotion {};
	hornMotion.prepare(rate, 48);
	drumMotion.prepare(rate, 40);
}

void RotaryEngine::start(CabinetModel selected, const RotarySettings& settings, const RotaryEngine* previous)
{
	model = selected;
	profile = profileFor(model);
	crossover.reset();
	crossover.setCutoffFrequency(std::min(profile.crossover, static_cast<float>(sampleRate * 0.4)));
	cabinet.reset();
	cabinet.setCutoffFrequency(std::min(profile.bandwidth, static_cast<float>(sampleRate * 0.4)));
	cabinet.setResonance(profile.resonance);
	for (auto* delays : { &hornDelay, &drumDelay }) for (auto& delay : *delays) delay.reset();
	hornToneFilters = {};
	drumToneFilters = {};
	hornDirectivity = {};
	drumDirectivity = {};
	requestedAngle = std::remainder(settings.angle, 360.0f) / 360.0f;
	angle.setCurrentAndTargetValue(requestedAngle);
	setSettings(settings);
	for (auto* smoother : { &balance, &angle, &distance, &hornTone, &drumTone }) smoother->reset();
	if (previous != nullptr)
	{
		hornMotion.seed(previous->hornMotion.getPhaseTurns(), previous->hornMotion.getCurrentRpm());
		drumMotion.seed(previous->drumMotion.getPhaseTurns(), previous->drumMotion.getCurrentRpm());
	}
	else
	{
		hornMotion.seed(0, hornMotion.getTargetRpm());
		drumMotion.seed(0, drumMotion.getTargetRpm());
	}
}

void RotaryEngine::setSettings(const RotarySettings& settings)
{
	balance.setTargetValue(std::clamp(settings.balance * 0.01f, -1.0f, 1.0f));
	const auto nextAngle = std::remainder(settings.angle, 360.0f) / 360.0f;
	if (std::abs(nextAngle - requestedAngle) > 1.0e-7f)
	{
		requestedAngle = nextAngle;
		angle.setTargetValue(angle.getCurrentValue() + std::remainder(nextAngle - angle.getCurrentValue(), 1.0f));
	}
	distance.setTargetValue(std::clamp(settings.distance, 0.3f, 3.0f));
	hornTone.setTargetValue(settings.hornTone);
	drumTone.setTargetValue(settings.drumTone);
	hornMotion.configure(settings.slow, settings.fast, settings.acceleration, settings.deceleration,
		settings.speedMode, settings.brake, settings.manual, settings.speedPosition, 1);
	drumMotion.configure(settings.slow * profile.drumSlowRatio, settings.fast * profile.drumFastRatio,
		settings.acceleration * profile.drumRise, settings.deceleration * profile.drumFall,
		settings.speedMode, settings.brake, settings.manual, settings.speedPosition,
		model == CabinetModel::drum ? 1.0f : -1.0f);
}

std::array<float, 2> RotaryEngine::speeds() const noexcept
{
	return { hornMotion.getCurrentRpm(), drumMotion.getCurrentRpm() };
}

std::array<float, 2> RotaryEngine::process(std::array<float, 2> input, bool autoFast) noexcept
{
	hornMotion.setAutoFast(autoFast);
	drumMotion.setAutoFast(autoFast);
	const auto rotation = angle.getNextValue();
	const auto hornPhase = hornMotion.advance();
	const auto drumPhase = drumMotion.advance();
	const auto micDistance = distance.getNextValue();
	const auto blend = balance.getNextValue();
	const auto highGain = std::pow(10.0f, hornTone.getNextValue() / 20.0f);
	const auto lowGain = std::pow(10.0f, drumTone.getNextValue() / 20.0f);
	const auto proximity = 0.3f / micDistance;
	const auto hornLevel = std::min(1.0f, 1.0f + blend);
	const auto drumLevel = std::min(1.0f, 1.0f - blend);
	const auto trebleCoefficient = coefficient(2400);
	const auto bassCoefficient = coefficient(300);
	std::array<float, 2> output {};
	for (std::size_t channel = 0; channel < 2; ++channel)
	{
		const auto micAngle = rotation + (channel == 0 ? -0.5f : 0.5f) * profile.separation;
		const auto hornFacing = std::cos(2.0f * std::numbers::pi_v<float> * (hornPhase - micAngle));
		const auto drumFacing = std::cos(2.0f * std::numbers::pi_v<float> * (drumPhase - micAngle));
		float low {}, high {};
		crossover.processSample(static_cast<int>(channel), input[channel], low, high);
		if (model == CabinetModel::drum) { low = input[channel]; high = 0; }
		high = highGain * high + (1.0f - highGain) * hornToneFilters[channel].lowpass(high, trebleCoefficient);
		low += (lowGain - 1.0f) * drumToneFilters[channel].lowpass(low, bassCoefficient);
		const auto cabinetFacing = 0.5f + 0.5f * std::cos(2.0f * std::numbers::pi_v<float> * micAngle);
		const auto air = 1.0f / (1.0f + 0.12f * (micDistance - 0.3f));
		const auto hornCutoff = (1800 + 10000 * (0.5f + 0.5f * hornFacing)) * air * (0.7f + 0.3f * cabinetFacing);
		const auto drumCutoff = (model == CabinetModel::drum ? 2200.0f : 1600.0f)
			+ 3000 * (0.5f + 0.5f * drumFacing);
		high = hornDirectivity[channel].lowpass(high, coefficient(hornCutoff));
		low = drumDirectivity[channel].lowpass(low, coefficient(drumCutoff * air));
		const auto hornAmplitude = 1.0f - profile.hornDepth * (0.35f + 0.65f * proximity) * (1.0f - hornFacing) * 0.5f;
		const auto drumAmplitude = 1.0f - profile.drumDepth * (0.35f + 0.65f * proximity) * (1.0f - drumFacing) * 0.5f;
		hornDelay[channel].pushSample(0, high * hornLevel * hornAmplitude);
		drumDelay[channel].pushSample(0, low * (model == CabinetModel::drum ? 1.0f : drumLevel) * drumAmplitude);
		const auto pickup = hornDelay[channel].popSample(0, pickupDelay(sampleRate, micDistance, profile.hornRadius, hornFacing))
			+ drumDelay[channel].popSample(0, pickupDelay(sampleRate, micDistance, profile.drumRadius, drumFacing));
		output[channel] = cabinet.processSample(static_cast<int>(channel), pickup);
	}
	return output;
}
}