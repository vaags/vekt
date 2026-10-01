#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace vekt::dsp
{
// The newest stereo samples of a signal, kept for an oscilloscope. One writer (the audio thread) and one reader (the
// message thread); publishing is wait-free and never allocates. A reader copying while the writer laps it can get a
// mix of older and newer samples, which only a single display frame shows.
class ScopeTap final
{
public:
	// About 85 ms at 192 kHz: room for a display window plus as long again to find a trigger in.
	static constexpr std::size_t capacity = 1u << 14;

	// Not while publish() runs (prepareToPlay, before processing).
	void prepare(double sampleRate) noexcept
	{
		rate.store(sampleRate, std::memory_order_relaxed);
		for (std::size_t index = 0; index < capacity; ++index)
		{
			left[index].store(0.0f, std::memory_order_relaxed);
			right[index].store(0.0f, std::memory_order_relaxed);
		}
		written.store(0, std::memory_order_release);
	}

	// Audio thread. A mono buffer feeds both channels; non-finite samples are stored as silence.
	void publish(const juce::AudioBuffer<float>& buffer) noexcept
	{
		const auto channels = buffer.getNumChannels();
		if (channels == 0) return;
		const auto* leftSamples = buffer.getReadPointer(0);
		const auto* rightSamples = buffer.getReadPointer(std::min(channels, 2) - 1);
		auto index = written.load(std::memory_order_relaxed);
		for (auto sample = 0; sample < buffer.getNumSamples(); ++sample, ++index)
		{
			left[index & mask].store(finiteOrSilence(leftSamples[sample]), std::memory_order_relaxed);
			right[index & mask].store(finiteOrSilence(rightSamples[sample]), std::memory_order_relaxed);
		}
		written.store(index, std::memory_order_release);
	}

	// Reader: fills both spans (equal length, at most capacity) with the newest samples, oldest first; positions before
	// the first published sample read as silence. Returns how many samples have been published in total.
	std::uint64_t readLatest(std::span<float> leftOut, std::span<float> rightOut) const noexcept
	{
		const auto end = written.load(std::memory_order_acquire);
		const auto count = std::min({ leftOut.size(), rightOut.size(), capacity });
		for (std::size_t offset = 0; offset < count; ++offset)
		{
			const auto missing = end + offset < count;
			const auto index = (end - count + offset) & mask;
			leftOut[offset] = missing ? 0.0f : left[index].load(std::memory_order_relaxed);
			rightOut[offset] = missing ? 0.0f : right[index].load(std::memory_order_relaxed);
		}
		return end;
	}

	// 0 before prepare().
	[[nodiscard]] double getSampleRate() const noexcept { return rate.load(std::memory_order_relaxed); }

private:
	static constexpr std::uint64_t mask = capacity - 1;
	static float finiteOrSilence(float sample) noexcept { return std::isfinite(sample) ? sample : 0.0f; }

	std::array<std::atomic<float>, capacity> left {}, right {};
	std::atomic<std::uint64_t> written {};
	std::atomic<double> rate {};
};

// Where a scope window of windowLength samples should start in samples so that a periodic signal stands still: at the
// newest rising zero crossing that leaves a whole window after it. A crossing counts only once the signal has gone
// below a tenth of its peak since the previous one, so noise around zero does not retrigger. Without one (silence,
// DC, or periods longer than the search) the window shows the newest samples instead.
[[nodiscard]] inline std::size_t findScopeTrigger(std::span<const float> samples, std::size_t windowLength) noexcept
{
	if (samples.size() <= windowLength) return 0;
	const auto latest = samples.size() - windowLength;
	auto peak = 0.0f;
	for (const auto sample : samples) peak = std::max(peak, std::abs(sample));
	const auto threshold = -std::max(peak * 0.1f, 1.0e-6f);
	std::optional<std::size_t> found;
	auto armed = false;
	for (std::size_t index = 1; index <= latest; ++index)
	{
		if (samples[index] < threshold)
			armed = true;
		else if (armed && samples[index - 1] < 0.0f && samples[index] >= 0.0f)
		{
			found = index;
			armed = false;
		}
	}
	return found.value_or(latest);
}
}
