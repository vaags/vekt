#include "AllocationCounter.h"

#include <pthread.h>

#include <atomic>
#include <cstdint>

extern "C"
{
	// libsystem_malloc's logging hook (malloc/malloc.h does not declare it); `type` carries the flags below.
	using MallocLogger = void(std::uint32_t type, std::uintptr_t argument1, std::uintptr_t argument2,
	    std::uintptr_t argument3, std::uintptr_t result, std::uint32_t framesToSkip);
	extern MallocLogger* malloc_logger;
}

namespace
{
constexpr std::uint32_t allocateFlag = 2; // MALLOC_LOG_TYPE_ALLOCATE
constexpr std::uint32_t deallocateFlag = 4; // MALLOC_LOG_TYPE_DEALLOCATE

// Not thread_local: the first access to a thread_local allocates, which would call the logger again.
std::atomic<pthread_t> countingThread { nullptr };
std::atomic<std::size_t> allocations { 0 };
std::atomic<std::size_t> releases { 0 };
MallocLogger* previousLogger = nullptr;

void countAllocation(std::uint32_t type, std::uintptr_t argument1, std::uintptr_t argument2, std::uintptr_t argument3,
    std::uintptr_t result, std::uint32_t framesToSkip)
{
	if (previousLogger != nullptr) previousLogger(type, argument1, argument2, argument3, result, framesToSkip);
	if (countingThread.load(std::memory_order_relaxed) != pthread_self()) return;
	if ((type & allocateFlag) != 0) allocations.fetch_add(1, std::memory_order_relaxed);
	if ((type & deallocateFlag) != 0) releases.fetch_add(1, std::memory_order_relaxed);
}
}

namespace vekt::test
{
AllocationScope::AllocationScope() noexcept
{
	allocations.store(0);
	releases.store(0);
	previousLogger = malloc_logger;
	countingThread.store(pthread_self());
	malloc_logger = countAllocation;
}

AllocationScope::~AllocationScope()
{
	if (active) static_cast<void>(stop());
}

AllocationCounts AllocationScope::stop() noexcept
{
	if (active)
	{
		malloc_logger = previousLogger;
		countingThread.store(nullptr);
		active = false;
	}
	return { allocations.load(), releases.load() };
}
}
