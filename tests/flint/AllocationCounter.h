#pragma once

#include <cstddef>

namespace vekt::test
{
struct AllocationCounts
{
	std::size_t allocations {};
	std::size_t releases {};

	[[nodiscard]] bool none() const noexcept { return allocations == 0 && releases == 0; }
};

// Counts the heap allocations and releases the calling thread makes while counting, for the real-time contract's "no
// heap allocation or release" (ARCHITECTURE.md). It hooks libmalloc's malloc_logger, the hook Instruments' allocation
// tracking uses, which sees malloc, the typed allocators, operator new and JUCE's HeapBlock alike. Patching the default
// malloc zone does not work: on macOS 27 malloc_default_zone() returns a forwarding stub that malloc never calls
// (measured 3 October 2026). Locks are not detected. Catch2's assertions allocate, so assert after stop().
// One scope at a time; the code under test must keep its allocations observable, since the optimizer may remove an
// unused malloc/free or new/delete pair.
class AllocationScope final
{
public:
	AllocationScope() noexcept;
	~AllocationScope();
	AllocationScope(const AllocationScope&) = delete;
	AllocationScope& operator=(const AllocationScope&) = delete;
	AllocationScope(AllocationScope&&) = delete;
	AllocationScope& operator=(AllocationScope&&) = delete;

	// Stops counting and returns what was counted.
	[[nodiscard]] AllocationCounts stop() noexcept;

private:
	bool active { true };
};
}
