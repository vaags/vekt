#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <semaphore>
#include <vector>

namespace vekt::mono
{
// The host's audio workgroup, handed from the thread that reports it (JUCE calls
// AudioProcessor::audioWorkgroupContextChanged from the audio/render callback) to helper threads. The
// publisher never waits: tryPublish takes the lock only if it is free and otherwise reports failure, so the
// caller keeps the value and retries on its next callback. Readers (helpers, pool creation) may wait.
class WorkgroupMailbox
{
public:
	[[nodiscard]] bool tryPublish(const juce::AudioWorkgroup& workgroup)
	{
		if (!lock.tryEnter()) return false;
		value = workgroup;
		lock.exit();
		generation.fetch_add(1, std::memory_order_release);
		return true;
	}
	[[nodiscard]] int currentGeneration() const noexcept { return generation.load(std::memory_order_acquire); }
	// Not for the audio thread: waits for a publisher that is copying.
	[[nodiscard]] juce::AudioWorkgroup read() const
	{
		const juce::SpinLock::ScopedLockType scoped(lock);
		return value;
	}

private:
	mutable juce::SpinLock lock;
	juce::AudioWorkgroup value;
	std::atomic<int> generation {};
};

// Real-time helper threads that render independent work units (groups of voices) alongside the audio
// thread. run() hands out unit indices from one atomic counter that the calling thread also drains, so a
// helper that wakes late only means the audio thread does more itself. Helpers sleep on a semaphore between
// runs, never allocate while rendering, and join the host's audio workgroup (macOS) when one is provided.
class MonoRenderWorkers
{
public:
	using Job = void (*)(void* context, int unit) noexcept;

	// Starts `threads` helpers; blockSize/sampleRate describe the host callback for the real-time policy. The
	// helpers (re)join the mailbox's workgroup whenever its generation changes; the mailbox must outlive them.
	MonoRenderWorkers(int threads, int blockSize, double sampleRate, const WorkgroupMailbox& workgroups);
	~MonoRenderWorkers();

	[[nodiscard]] int threads() const noexcept { return static_cast<int>(workers.size()); }
	// Runs job(context, unit) for every unit in [0, units) and returns when all have finished.
	void run(int units, Job job, void* context) noexcept;

	// Performance cores minus one (the audio thread's), capped at three helpers.
	[[nodiscard]] static int defaultThreadCount() noexcept;

private:
	class Worker;
	void drain() noexcept;

	std::vector<std::unique_ptr<Worker>> workers;
	std::counting_semaphore<> wake { 0 };
	std::atomic<bool> exiting {};
	std::atomic<Job> job {};
	std::atomic<void*> context {};
	// (unit count << 32) | next unit, published per run so a claim always carries its own run's limit.
	std::atomic<std::uint64_t> claims {};
	std::atomic<int> remaining {};
	const WorkgroupMailbox& mailbox;
};
}
