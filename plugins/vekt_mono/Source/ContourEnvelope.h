#pragma once

#include <bit>
#include <cmath>
#include <cstdint>

namespace vekt::mono
{
// Times denote 99% of the distance to the target. The remaining quiet tail
// continues to 99.99% before snapping so the endpoint is effectively silent.
// State and coefficients are double: at high internal rates a step toward the
// target falls below float rounding and the stage would stall (ARCHITECTURE.md).
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
	void reset() noexcept { value = 0.0; state = State::idle; }
	void noteOn() noexcept { state = State::attack; }
	void noteOff() noexcept { if (state != State::idle) { releaseStart = value; state = State::release; } }
	[[nodiscard]] bool isActive() const noexcept { return state != State::idle; }
	[[nodiscard]] float getNextSample() noexcept
	{
		switch (state)
		{
		case State::idle: return 0.0f;
		case State::attack:
			advance(1.0, parameters.attack, attackCoefficient);
			// The stage time lands on 99 %. A float time widened to double is off by up to 2^-24 of itself, which
			// moves the level there by up to 3e-9; within 1e-8 the attack ends at its time, not a sample later.
			if (value >= 0.99 - 1.0e-8) { value = 1.0; state = State::decay; }
			break;
		case State::decay:
		{
			const double sustain = parameters.sustain;
			advance(sustain, parameters.decay, decayCoefficient);
			if (value <= sustain || std::abs(value - sustain) <= endpointThreshold * (1.0 - sustain))
			{
				value = sustain; state = State::sustain;
			}
			break;
		}
		case State::sustain: value = parameters.sustain; break;
		case State::release:
		{
			advance(0.0, parameters.release, releaseCoefficient);
			if (value <= endpointThreshold * releaseStart) reset();
			break;
		}
		}
		return static_cast<float>(value);
	}
private:
	enum class State { idle, attack, decay, sustain, release };
	static constexpr double endpointThreshold = 0.0001;
	void recalculate() noexcept
	{
		const auto coefficient = [this](float seconds)
		{
			return seconds > 0.0f ? std::exp(-std::log(100.0) / (static_cast<double>(seconds) * sampleRate)) : 0.0;
		};
		attackCoefficient = coefficient(parameters.attack);
		decayCoefficient = coefficient(parameters.decay);
		releaseCoefficient = coefficient(parameters.release);
	}
	void advance(double target, float seconds, double coefficient) noexcept
	{
		if (seconds <= 0.0f) { value = target; return; }
		value = target + (value - target) * coefficient;
	}
	Parameters parameters;
	double sampleRate { 48'000.0 };
	double value {}, releaseStart { 1.0 };
	double attackCoefficient {}, decayCoefficient {}, releaseCoefficient {};
	State state { State::idle };
};
}