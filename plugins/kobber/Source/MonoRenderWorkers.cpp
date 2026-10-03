#include "MonoRenderWorkers.h"

#include <algorithm>

#if JUCE_MAC
 #include <sys/sysctl.h>
#endif

namespace vekt::mono
{
class MonoRenderWorkers::Worker final : public juce::Thread
{
public:
	explicit Worker(MonoRenderWorkers& pool, int index) : juce::Thread("Mono render " + juce::String(index)), owner(pool) {}

	void run() override
	{
		juce::WorkgroupToken token;
		auto joinedGeneration = -1;
		while (!threadShouldExit())
		{
			owner.wake.acquire();
			if (owner.exiting.load(std::memory_order_acquire)) break;
			// Before claiming any unit: a helper waiting here for the mailbox holds no work the audio thread needs.
			if (const auto generation = owner.mailbox.currentGeneration(); generation != joinedGeneration)
			{
				const auto current = owner.mailbox.read();
				token.reset();
				if (current) current.join(token);
				joinedGeneration = generation;
			}
			owner.drain();
		}
	}

private:
	MonoRenderWorkers& owner;
};

MonoRenderWorkers::MonoRenderWorkers(int threads, int blockSize, double sampleRate, const WorkgroupMailbox& workgroups)
	: mailbox(workgroups)
{
	const auto options = juce::Thread::RealtimeOptions {}.withApproximateAudioProcessingTime(std::max(blockSize, 1),
		sampleRate > 0.0 ? sampleRate : 48'000.0);
	for (int index = 0; index < threads; ++index)
	{
		auto worker = std::make_unique<Worker>(*this, index);
		if (worker->startRealtimeThread(options)) workers.push_back(std::move(worker));
	}
}

MonoRenderWorkers::~MonoRenderWorkers()
{
	exiting.store(true, std::memory_order_release);
	for (auto& worker : workers) worker->signalThreadShouldExit();
	wake.release(static_cast<std::ptrdiff_t>(workers.size()));
	for (auto& worker : workers) worker->stopThread(2'000);
}

void MonoRenderWorkers::drain() noexcept
{
	for (;;)
	{
		// The claim's high half is its own run's unit count, so a late helper never runs a unit from another run.
		const auto claim = claims.fetch_add(1, std::memory_order_acq_rel);
		const auto unit = static_cast<int>(claim & 0xffff'ffffu);
		if (unit >= static_cast<int>(claim >> 32)) return;
		job.load(std::memory_order_relaxed)(context.load(std::memory_order_relaxed), unit);
		remaining.fetch_sub(1, std::memory_order_release);
	}
}

void MonoRenderWorkers::run(int units, Job newJob, void* newContext) noexcept
{
	if (units <= 0) return;
	job.store(newJob, std::memory_order_relaxed);
	context.store(newContext, std::memory_order_relaxed);
	remaining.store(units, std::memory_order_relaxed);
	claims.store(static_cast<std::uint64_t>(units) << 32, std::memory_order_release);
	if (const auto helpers = std::min(static_cast<int>(workers.size()), units - 1); helpers > 0)
		wake.release(helpers);
	drain();
	// Only units a helper has already started can remain; wait for them.
	while (remaining.load(std::memory_order_acquire) > 0)
	{
#if defined(__aarch64__)
		__builtin_arm_yield();
#endif
	}
}

int MonoRenderWorkers::defaultThreadCount() noexcept
{
	int performanceCores {};
#if JUCE_MAC
	auto size = sizeof(performanceCores);
	if (sysctlbyname("hw.perflevel0.physicalcpu", &performanceCores, &size, nullptr, 0) != 0) performanceCores = 0;
#endif
	if (performanceCores <= 0) performanceCores = juce::SystemStats::getNumPhysicalCpus();
	return std::clamp(performanceCores - 1, 0, 7);
}
}
