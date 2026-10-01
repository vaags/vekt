#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace vekt::dsp
{
// One block's display values, stamped with the sample position at the end of the block.
template <std::size_t Channels>
struct DisplayFrame
{
	std::uint64_t sample {};
	double sampleRate {};
	// Identifies what the values describe (for example the voice they were taken from); 0 means nothing to show.
	std::uint64_t tag {};
	std::array<float, Channels> values {};
};

// Values the audio thread publishes once per block, for displays that animate them smoothly. One writer (the audio
// thread) and one reader (the message thread); publishing is wait-free and never allocates. A reader that falls more
// than Capacity frames behind loses the oldest ones.
template <std::size_t Channels, std::size_t Capacity = 64>
class DisplayHistory
{
public:
	using Frame = DisplayFrame<Channels>;

	// Audio thread.
	void publish(const Frame& frame) noexcept
	{
		const auto index = written.load(std::memory_order_relaxed);
		auto& slot = slots[index % Capacity];
		// Odd while the slot is being written, so a reader can tell a torn copy.
		const auto sequence = slot.sequence.load(std::memory_order_relaxed);
		slot.sequence.store(sequence + 1, std::memory_order_relaxed);
		std::atomic_thread_fence(std::memory_order_release);
		slot.sample.store(frame.sample, std::memory_order_relaxed);
		slot.sampleRate.store(frame.sampleRate, std::memory_order_relaxed);
		slot.tag.store(frame.tag, std::memory_order_relaxed);
		for (std::size_t channel = 0; channel < Channels; ++channel)
			slot.values[channel].store(frame.values[channel], std::memory_order_relaxed);
		slot.sequence.store(sequence + 2, std::memory_order_release);
		written.store(index + 1, std::memory_order_release);
	}

	// Reader: passes each frame published since the previous call to `use`, oldest first.
	template <typename Use>
	void readNew(Use&& use) noexcept
	{
		const auto available = written.load(std::memory_order_acquire);
		if (available - read > Capacity) read = available - Capacity;
		for (; read < available; ++read)
		{
			const auto& slot = slots[read % Capacity];
			const auto before = slot.sequence.load(std::memory_order_acquire);
			if ((before & 1u) != 0) continue;
			Frame frame;
			frame.sample = slot.sample.load(std::memory_order_relaxed);
			frame.sampleRate = slot.sampleRate.load(std::memory_order_relaxed);
			frame.tag = slot.tag.load(std::memory_order_relaxed);
			for (std::size_t channel = 0; channel < Channels; ++channel)
				frame.values[channel] = slot.values[channel].load(std::memory_order_relaxed);
			std::atomic_thread_fence(std::memory_order_acquire);
			// Overwritten while copying: the writer has lapped this reader, so the frame is gone.
			if (slot.sequence.load(std::memory_order_relaxed) != before) continue;
			use(frame);
		}
	}

private:
	struct Slot
	{
		std::atomic<std::uint32_t> sequence {};
		std::atomic<std::uint64_t> sample {};
		std::atomic<double> sampleRate {};
		std::atomic<std::uint64_t> tag {};
		std::array<std::atomic<float>, Channels> values {};
	};

	std::array<Slot, Capacity> slots {};
	std::atomic<std::uint64_t> written {};
	std::uint64_t read {}; // the reader's own position
};

// Turns a DisplayHistory into smooth motion: each display frame shows the values from a short, self-adjusting delay
// in the past, interpolated between the blocks either side, so blocks arriving at any size, unevenly or in bursts
// still move steadily at any refresh rate. The delay covers the longest recent gap in publishing (the latest couple
// of seconds' worst) and is capped. It changes gradually, since every change of delay speeds up or slows down what
// is shown; it rises faster than it falls, because too short a delay holds the display still. Message thread only.
template <std::size_t Channels, std::size_t Capacity = 64>
class DisplayTimeline
{
public:
	using Frame = DisplayFrame<Channels>;

	// The values to show in a display frame presented at nowSeconds (any monotonic clock); empty before any block.
	[[nodiscard]] std::optional<Frame> advance(DisplayHistory<Channels, Capacity>& history, double nowSeconds) noexcept
	{
		auto arrived = false;
		history.readNew([&](const Frame& frame)
		{
			// The audio side restarted its count or changed rate: what is stored no longer lines up.
			if (count > 0 && (frame.sample <= newest().sample || std::abs(frame.sampleRate - newest().sampleRate) > 0.5)) restart();
			push(frame);
			arrived = true;
		});
		if (count == 0) return {};
		const auto& latest = newest();
		const auto rate = latest.sampleRate > 0.0 ? latest.sampleRate : 48'000.0;
		const auto frontier = static_cast<double>(latest.sample);
		const auto elapsed = anchored ? nowSeconds - lastNow : 0.0;
		if (!anchored || elapsed < 0.0 || elapsed > discontinuitySeconds) reanchor(frontier, nowSeconds, rate);
		auto clock = clockAt(nowSeconds, rate);
		if (arrived)
		{
			const auto error = frontier - clock;
			if (std::abs(error) > discontinuitySeconds * rate)
				reanchor(frontier, nowSeconds, rate);
			else
				anchorSample += error * clockCorrection;
			clock = clockAt(nowSeconds, rate);
		}
		// How far the clock has run ahead of the newest block: the delay must cover the largest recent gap.
		const auto lag = clock - frontier;
		if (nowSeconds - windowStart >= lagWindowSeconds)
		{
			previousWindowLag = currentWindowLag;
			currentWindowLag = 0.0;
			windowStart = nowSeconds;
		}
		currentWindowLag = std::max(currentWindowLag, lag);
		const auto wanted = std::clamp(std::max(currentWindowLag, previousWindowLag) + marginSeconds * rate,
			minimumDelaySeconds * rate, maximumDelaySeconds * rate);
		const auto step = std::max(0.0, nowSeconds - lastNow) * rate;
		delay = std::clamp(wanted, delay - delayFall * step, delay + delayRise * step);
		lastNow = nowSeconds;
		// Never backwards, never past the newest block, never before the oldest kept.
		auto target = std::min(std::max(clock - delay, lastTarget), frontier);
		target = std::max(target, static_cast<double>(oldest().sample));
		lastTarget = target;
		return frameAt(target);
	}

	// The current display delay, in seconds.
	[[nodiscard]] double getDelaySeconds() const noexcept
	{
		return count > 0 && newest().sampleRate > 0.0 ? delay / newest().sampleRate : 0.0;
	}

	void restart() noexcept
	{
		count = 0;
		anchored = false;
		lastTarget = 0.0;
	}

private:
	[[nodiscard]] const Frame& newest() const noexcept { return frames[(start + count - 1) % Capacity]; }
	[[nodiscard]] const Frame& oldest() const noexcept { return frames[start]; }
	[[nodiscard]] const Frame& at(std::size_t index) const noexcept { return frames[(start + index) % Capacity]; }

	void push(const Frame& frame) noexcept
	{
		if (count == Capacity)
		{
			start = (start + 1) % Capacity;
			--count;
		}
		frames[(start + count) % Capacity] = frame;
		++count;
	}

	// Starts the clock at the newest block, forgetting past gaps; the delay starts at its minimum and grows to fit.
	void reanchor(double frontier, double nowSeconds, double rate) noexcept
	{
		anchorSample = frontier;
		anchorTime = nowSeconds;
		lastNow = nowSeconds;
		windowStart = nowSeconds;
		currentWindowLag = previousWindowLag = 0.0;
		delay = minimumDelaySeconds * rate;
		anchored = true;
	}

	[[nodiscard]] double clockAt(double nowSeconds, double rate) const noexcept
	{
		return anchorSample + (nowSeconds - anchorTime) * rate;
	}

	// Linear between the blocks either side of target; frames describing different things (tags) are not blended,
	// so the earlier one holds until the later one's time.
	[[nodiscard]] Frame frameAt(double target) const noexcept
	{
		for (std::size_t index = count; index-- > 1;)
		{
			const auto& before = at(index - 1);
			const auto& after = at(index);
			if (static_cast<double>(before.sample) > target) continue;
			if (static_cast<double>(after.sample) <= target) return after;
			auto frame = before;
			if (before.tag != after.tag) return frame;
			const auto span = static_cast<double>(after.sample - before.sample);
			const auto blend = static_cast<float>((target - static_cast<double>(before.sample)) / span);
			for (std::size_t channel = 0; channel < Channels; ++channel)
				frame.values[channel] += (after.values[channel] - before.values[channel]) * blend;
			frame.sample = static_cast<std::uint64_t>(target);
			return frame;
		}
		return oldest();
	}

	static constexpr double minimumDelaySeconds = 0.004;
	static constexpr double maximumDelaySeconds = 0.25;
	static constexpr double marginSeconds = 0.003;
	// The longest gap is tracked over two windows of this length, so it is forgotten one to two windows after.
	static constexpr double lagWindowSeconds = 1.0;
	// How much the delay may change per second of display time, as a share of real time: what is shown runs at 75 %
	// speed at most while the delay grows, and at 105 % while it shrinks.
	static constexpr double delayRise = 0.25;
	static constexpr double delayFall = 0.05;
	// Share of each new block's clock error corrected at once; the rest is smoothed away.
	static constexpr double clockCorrection = 0.05;
	// A clock error or display-time jump this large starts the timeline afresh (transport reset, hidden editor).
	static constexpr double discontinuitySeconds = 0.5;

	std::array<Frame, Capacity> frames {};
	std::size_t start {}, count {};
	bool anchored {};
	double anchorSample {}, anchorTime {}, lastNow {}, delay {}, lastTarget {};
	double windowStart {}, currentWindowLag {}, previousWindowLag {};
};
}
