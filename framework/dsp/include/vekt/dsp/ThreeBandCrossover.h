#pragma once

#include <vekt/dsp/ControlTransition.h>

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <concepts>
#include <numbers>

namespace vekt::dsp
{
template <std::floating_point Sample>
class ThreeBandCrossover final
{
public:
	struct Cutoffs
	{
		Sample lowMidHz { static_cast<Sample>(250) };
		Sample midHighHz { static_cast<Sample>(2'500) };

	};

	struct Bands
	{
		juce::dsp::AudioBlock<Sample> low;
		juce::dsp::AudioBlock<Sample> mid;
		juce::dsp::AudioBlock<Sample> high;
	};

	void prepare(const juce::dsp::ProcessSpec& specification, Cutoffs initialCutoffs)
	{
		sampleRate = static_cast<Sample>(specification.sampleRate);
		channelCount = std::min<std::size_t>(specification.numChannels, maxChannels);
		const auto bounded = clampCutoffs(initialCutoffs);
		lowMidCutoff.prepare(specification.sampleRate);
		midHighCutoff.prepare(specification.sampleRate);
		lowMidCutoff.setCurrentAndTargetValue(bounded.lowMidHz);
		midHighCutoff.setCurrentAndTargetValue(bounded.midHighHz);
		requested = active = bounded;
		reset();
	}

	void reset() noexcept
	{
		active = requested;
		lowMidCutoff.setCurrentAndTargetValue(active.lowMidHz);
		midHighCutoff.setCurrentAndTargetValue(active.midHighHz);
		for (auto& split : splits)
			split.reset();
	}

	void requestCutoffs(Cutoffs cutoffs) noexcept
	{
		requested = clampCutoffs(cutoffs);
		lowMidCutoff.setTargetValue(requested.lowMidHz);
		midHighCutoff.setTargetValue(requested.midHighHz);
	}

	void process(const juce::dsp::AudioBlock<const Sample>& input, Bands output) noexcept
	{
		jassert(input.getNumChannels() <= channelCount);
		jassert(output.low.getNumChannels() >= input.getNumChannels());
		jassert(output.mid.getNumChannels() >= input.getNumChannels());
		jassert(output.high.getNumChannels() >= input.getNumChannels());
		jassert(output.low.getNumSamples() >= input.getNumSamples());
		jassert(output.mid.getNumSamples() >= input.getNumSamples());
		jassert(output.high.getNumSamples() >= input.getNumSamples());

		for (std::size_t sample = 0; sample < input.getNumSamples(); ++sample)
		{
			const auto lowMid = lowMidCutoff.getNextValue();
			const auto midHigh = midHighCutoff.getNextValue();
			active = { lowMid, midHigh };
			for (std::size_t channel = 0; channel < input.getNumChannels(); ++channel)
			{
				const auto channelIndex = static_cast<int>(channel);
				const auto sampleIndex = static_cast<int>(sample);
				const auto inputSample = input.getSample(channelIndex, sampleIndex);
				const auto low = splits[0].low.process(inputSample, channel, lowMid, sampleRate);
				const auto remainder = inputSample - low;
				const auto mid = splits[1].low.process(remainder, channel, midHigh, sampleRate);
				output.low.setSample(channelIndex, sampleIndex, low);
				output.mid.setSample(channelIndex, sampleIndex, mid);
				output.high.setSample(channelIndex, sampleIndex, remainder - mid);
			}
		}
	}

	[[nodiscard]] Cutoffs getActiveCutoffs() const noexcept { return active; }
	[[nodiscard]] Cutoffs getRequestedCutoffs() const noexcept { return requested; }

private:
	static constexpr std::size_t maxChannels = 2;
	struct Biquad
	{
		struct State
		{
			Sample z1 {};
			Sample z2 {};
		};

		void reset() noexcept
		{
			for (auto& state : states)
				state = {};
		}

		Sample process(Sample input, std::size_t channel, Sample cutoffHz, Sample sampleRateHz) noexcept
		{
			jassert(channel < states.size());
			setCoefficients(cutoffHz, sampleRateHz);
			Sample output = input;
			auto& state = states[channel];
			const auto first = b0 * output + state.z1;
			state.z1 = b1 * output - a1 * first + state.z2;
			state.z2 = b2 * output - a2 * first;
			const auto second = b0 * first + state.z1;
			state.z1 = b1 * first - a1 * second + state.z2;
			state.z2 = b2 * first - a2 * second;
			output = second;
			return output;
		}

		void setCoefficients(Sample cutoffHz, Sample sampleRateHz) noexcept
		{
			const auto bounded = std::clamp(cutoffHz,
				static_cast<Sample>(1), sampleRateHz * static_cast<Sample>(0.45));
			const auto omega = static_cast<Sample>(2) * std::numbers::pi_v<Sample>
				* bounded / sampleRateHz;
			const auto sine = std::sin(omega);
			const auto cosine = std::cos(omega);
			const auto alpha = sine / (static_cast<Sample>(2) * std::sqrt(static_cast<Sample>(0.5)));
			const auto a0 = static_cast<Sample>(1) + alpha;
			const auto sign = highPass ? static_cast<Sample>(-1) : static_cast<Sample>(1);
			b0 = (static_cast<Sample>(1) + sign * cosine) / (static_cast<Sample>(2) * a0);
			b1 = (static_cast<Sample>(1) + sign * cosine) * static_cast<Sample>(-1) / a0;
			b2 = b0;
			a1 = static_cast<Sample>(-2) * cosine / a0;
			a2 = (static_cast<Sample>(1) - alpha) / a0;
		}

		bool highPass {};
		Sample b0 { 1 };
		Sample b1 {};
		Sample b2 {};
		Sample a1 {};
		Sample a2 {};
		std::array<State, maxChannels> states {};
	};

	struct Split
	{
		Split() : high { true } {}
		Biquad low;
		Biquad high;

		void reset() noexcept
		{
			low.reset();
			high.reset();
		}
	};

	[[nodiscard]] Cutoffs clampCutoffs(Cutoffs cutoffs) const noexcept
	{
		const auto nyquistLimit = sampleRate * static_cast<Sample>(0.45);
		cutoffs.lowMidHz = std::clamp(cutoffs.lowMidHz, static_cast<Sample>(20), nyquistLimit);
		cutoffs.midHighHz = std::clamp(cutoffs.midHighHz, static_cast<Sample>(40), nyquistLimit);
		cutoffs.midHighHz = std::max(cutoffs.midHighHz, cutoffs.lowMidHz * static_cast<Sample>(2));
		cutoffs.midHighHz = std::min(cutoffs.midHighHz, nyquistLimit);
		cutoffs.lowMidHz = std::min(cutoffs.lowMidHz, cutoffs.midHighHz / static_cast<Sample>(2));
		return cutoffs;
	}

	Sample sampleRate { static_cast<Sample>(48'000) };
	std::size_t channelCount {};
	ControlTransition<Sample> lowMidCutoff;
	ControlTransition<Sample> midHighCutoff;
	Cutoffs active {};
	Cutoffs requested {};
	std::array<Split, 2> splits;
};
}
