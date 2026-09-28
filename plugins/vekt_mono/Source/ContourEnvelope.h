#pragma once

#include <bit>
#include <cmath>
#include <cstdint>

namespace vekt::mono
{
// Times denote 99% of the distance to the target. Snap at that point
// so each segment has a finite duration.
class ContourEnvelope
{
public:
	struct Parameters
	{
		float attack {}, decay {}, sustain {}, release {};
	};

	void setSampleRate(double rate) noexcept { sampleRate = rate > 0.0 ? rate : 48'000.0; recalculate(); }
	void setParameters(Parameters next) noexcept
	{
		if (std::bit_cast<std::uint32_t>(next.attack) == std::bit_cast<std::uint32_t>(parameters.attack)
			&& std::bit_cast<std::uint32_t>(next.decay) == std::bit_cast<std::uint32_t>(parameters.decay)
			&& std::bit_cast<std::uint32_t>(next.sustain) == std::bit_cast<std::uint32_t>(parameters.sustain)
			&& std::bit_cast<std::uint32_t>(next.release) == std::bit_cast<std::uint32_t>(parameters.release)) return;
		parameters = next;
		recalculate();
	}
	void reset() noexcept { value = 0.0f; state = State::idle; }
	void noteOn() noexcept { state = State::attack; }
	void noteOff() noexcept { if (state != State::idle) { releaseStart = value; state = State::release; } }
	[[nodiscard]] bool isActive() const noexcept { return state != State::idle; }
	[[nodiscard]] float getNextSample() noexcept
	{
		switch (state)
		{
		case State::idle: return 0.0f;
		case State::attack:
			advance(1.0f, parameters.attack, attackCoefficient);
			if (value >= 0.99f) { value = 1.0f; state = State::decay; }
			break;
		case State::decay:
			advance(parameters.sustain, parameters.decay, decayCoefficient);
			if (value <= parameters.sustain
				|| std::abs(value - parameters.sustain) <= 0.01f * (1.0f - parameters.sustain))
			{
				value = parameters.sustain; state = State::sustain;
			}
			break;
		case State::sustain: value = parameters.sustain; break;
		case State::release:
		{
			advance(0.0f, parameters.release, releaseCoefficient);
			if (value <= 0.01f * releaseStart) reset();
			break;
		}
		}
		return value;
	}
private:
	enum class State { idle, attack, decay, sustain, release };
	void recalculate() noexcept
	{
		const auto coefficient = [this](float seconds)
		{
			return seconds > 0.0f ? static_cast<float>(std::exp(-std::log(100.0) / (static_cast<double>(seconds) * sampleRate))) : 0.0f;
		};
		attackCoefficient = coefficient(parameters.attack);
		decayCoefficient = coefficient(parameters.decay);
		releaseCoefficient = coefficient(parameters.release);
	}
	void advance(float target, float seconds, float coefficient) noexcept
	{
		if (seconds <= 0.0f) { value = target; return; }
		value = target + (value - target) * coefficient;
	}
	Parameters parameters;
	double sampleRate { 48'000.0 };
	float value {}, releaseStart { 1.0f };
	float attackCoefficient {}, decayCoefficient {}, releaseCoefficient {};
	State state { State::idle };
};
}