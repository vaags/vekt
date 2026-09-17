#pragma once

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <concepts>

namespace vekt::rav
{
template <std::floating_point Sample>
class AdaptiveAutoGain final
{
public:
	void prepare(double sampleRate, double rampDurationSeconds = 0.02)
	{
		gain.reset(sampleRate, rampDurationSeconds);
		gain.setCurrentAndTargetValue(gain.getTargetValue());
	}

	void reset() noexcept
	{
		gain.setCurrentAndTargetValue(gain.getTargetValue());
	}

	void process(
		const juce::dsp::AudioBlock<const Sample>& reference,
		juce::dsp::AudioBlock<Sample> wet,
		bool enabled) noexcept
	{
		if (enabled)
		{
			const auto referenceRms = rms(reference);
			const auto wetRms = rms(juce::dsp::AudioBlock<const Sample>(wet));
			if (wetRms > minimumSignal)
			{
				const auto target = std::clamp(
					referenceRms / wetRms, minimumGain, Sample { 1 });
				gain.setTargetValue(target);
			}
		}
		else
		{
			gain.setTargetValue(Sample { 1 });
		}

		for (std::size_t sample = 0; sample < wet.getNumSamples(); ++sample)
		{
			const auto currentGain = gain.getNextValue();
			for (std::size_t channel = 0; channel < wet.getNumChannels(); ++channel)
				wet.getChannelPointer(channel)[sample] *= currentGain;
		}
	}

private:
	[[nodiscard]] static Sample rms(const juce::dsp::AudioBlock<const Sample>& block) noexcept
	{
		if (block.getNumChannels() == 0 || block.getNumSamples() == 0)
			return {};

		long double sumOfSquares = 0.0L;
		const auto sampleCount = block.getNumChannels() * block.getNumSamples();
		for (std::size_t channel = 0; channel < block.getNumChannels(); ++channel)
			for (std::size_t sample = 0; sample < block.getNumSamples(); ++sample)
			{
				const auto value = static_cast<long double>(block.getSample(
					static_cast<int>(channel), static_cast<int>(sample)));
				if (std::isfinite(static_cast<double>(value)))
					sumOfSquares += value * value;
			}

		return static_cast<Sample>(std::sqrt(sumOfSquares / static_cast<long double>(sampleCount)));
	}

	inline static constexpr Sample minimumGain { static_cast<Sample>(0.06309573444801933) };
	inline static constexpr Sample minimumSignal { static_cast<Sample>(1.0e-9) };
	juce::SmoothedValue<Sample, juce::ValueSmoothingTypes::Linear> gain { Sample { 1 } };
};
}
