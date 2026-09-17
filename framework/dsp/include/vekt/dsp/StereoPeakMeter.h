#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <cmath>
#include <span>

namespace vekt::dsp
{
class StereoPeakMeter final
{
public:
	void publish(const juce::AudioBuffer<float>& buffer) noexcept
	{
		for (auto channel = 0; channel < std::min(buffer.getNumChannels(), 2); ++channel)
		{
			auto peak = 0.0f;
			for (const auto sample : std::span(buffer.getReadPointer(channel),
				static_cast<std::size_t>(buffer.getNumSamples())))
				if (std::isfinite(sample))
					peak = std::max(peak, std::abs(sample));
			publishPeak(peaks[static_cast<std::size_t>(channel)], peak);
		}
	}

	[[nodiscard]] std::array<float, 2> consumePeaks() noexcept
	{
		return { peaks[0].exchange(0.0f), peaks[1].exchange(0.0f) };
	}

	void reset() noexcept
	{
		for (auto& peak : peaks)
			peak.store(0.0f);
	}

private:
	static void publishPeak(std::atomic<float>& destination, float value) noexcept
	{
		auto observed = destination.load(std::memory_order_relaxed);
		while (observed < value && !destination.compare_exchange_weak(
			observed, value, std::memory_order_relaxed, std::memory_order_relaxed))
		{
		}
	}

	std::array<std::atomic<float>, 2> peaks {};
};
}
