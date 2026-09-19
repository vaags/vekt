#pragma once

#include "RotorMotion.h"
#include <vekt/dsp/ControlTransition.h>
#include <juce_dsp/juce_dsp.h>
#include <array>

namespace vekt::glimmer
{
enum class CabinetModel { classic, drum, wide };

struct RotarySettings
{
	float balance {};
	float angle {};
	float distance { 1.0f };
	float hornTone {};
	float drumTone {};
	float slow { 48.0f };
	float fast { 400.0f };
	float acceleration { 2.0f };
	float deceleration { 4.0f };
	RotarySpeedMode speedMode { RotarySpeedMode::slow };
	bool brake {};
	bool manual {};
	float speedPosition {};
};

class RotaryEngine final
{
public:
	void prepare(double sampleRate, int maximumBlockSize);
	void start(CabinetModel model, const RotarySettings& settings, const RotaryEngine* previous = nullptr);
	void setSettings(const RotarySettings& settings);
	[[nodiscard]] std::array<float, 2> process(std::array<float, 2> input, bool autoFast) noexcept;
	[[nodiscard]] std::array<float, 2> speeds() const noexcept;
	[[nodiscard]] CabinetModel getModel() const noexcept { return model; }
	[[nodiscard]] static int latencySamples(double sampleRate) noexcept;
	[[nodiscard]] static int capacitySamples(double sampleRate) noexcept;
	[[nodiscard]] static float pickupDelay(double sampleRate, float distance, float radius, float facing) noexcept;
	[[nodiscard]] static std::array<float, 2> applyWidth(std::array<float, 2> input, float width) noexcept;

private:
	struct Profile
	{
		float crossover;
		float drumSlowRatio;
		float drumFastRatio;
		float drumRise;
		float drumFall;
		float separation;
		float hornRadius;
		float drumRadius;
		float bandwidth;
		float resonance;
		float hornDepth;
		float drumDepth;
	};
	struct Pole
	{
		float state {};
		float lowpass(float input, float coefficient) noexcept
		{
			state += coefficient * (input - state);
			return state;
		}
	};
	[[nodiscard]] float coefficient(float frequency) const noexcept;
	[[nodiscard]] static Profile profileFor(CabinetModel model) noexcept;
	using Delay = juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd>;
	double sampleRate { 48'000.0 };
	CabinetModel model { CabinetModel::classic };
	Profile profile {};
	RotorMotion hornMotion;
	RotorMotion drumMotion;
	juce::dsp::LinkwitzRileyFilter<float> crossover;
	juce::dsp::StateVariableTPTFilter<float> cabinet;
	std::array<Delay, 2> hornDelay;
	std::array<Delay, 2> drumDelay;
	std::array<Pole, 2> hornToneFilters;
	std::array<Pole, 2> drumToneFilters;
	std::array<Pole, 2> hornDirectivity;
	std::array<Pole, 2> drumDirectivity;
	dsp::ControlTransition<float> balance;
	dsp::ControlTransition<float> angle;
	dsp::ControlTransition<float> distance;
	dsp::ControlTransition<float> hornTone;
	dsp::ControlTransition<float> drumTone;
	float requestedAngle {};
};
}