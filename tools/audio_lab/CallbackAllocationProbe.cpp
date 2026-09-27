#include "CallbackAllocationProbe.h"

#include <cstddef>
#include <cstdlib>
#include <new>

namespace
{
thread_local bool measuring {};
thread_local std::uint64_t allocations {};

void* allocate(std::size_t size)
{
	if (auto* memory = std::malloc(size == 0 ? 1 : size))
	{
		if (measuring) ++allocations;
		return memory;
	}
	throw std::bad_alloc {};
}

void* allocateAligned(std::size_t size, std::align_val_t alignment)
{
	void* memory {};
	if (posix_memalign(&memory, static_cast<std::size_t>(alignment), size == 0 ? 1 : size) == 0)
	{
		if (measuring) ++allocations;
		return memory;
	}
	throw std::bad_alloc {};
}

void* allocateNoThrow(std::size_t size) noexcept
{
	if (auto* memory = std::malloc(size == 0 ? 1 : size))
	{
		if (measuring) ++allocations;
		return memory;
	}
	return nullptr;
}

void* allocateAlignedNoThrow(std::size_t size, std::align_val_t alignment) noexcept
{
	void* memory {};
	if (posix_memalign(&memory, static_cast<std::size_t>(alignment), size == 0 ? 1 : size) == 0)
	{
		if (measuring) ++allocations;
		return memory;
	}
	return nullptr;
}

// Escape verification allocations so Release LTO cannot elide new/delete.
volatile void* verifiedAllocation {};
volatile void* verifiedAlignedAllocation {};
volatile void* verifiedScalarAllocation {};
volatile void* verifiedNothrowScalar {};
volatile void* verifiedNothrowArray {};
volatile void* verifiedNothrowAligned {};
volatile void* verifiedNothrowAlignedArray {};

void releaseVerificationAllocations() noexcept
{
	delete[] static_cast<char*>(const_cast<void*>(verifiedAllocation));
	::operator delete(const_cast<void*>(verifiedAlignedAllocation), std::align_val_t { 64 });
	delete static_cast<char*>(const_cast<void*>(verifiedScalarAllocation));
	::operator delete(const_cast<void*>(verifiedNothrowScalar), std::nothrow);
	::operator delete[](const_cast<void*>(verifiedNothrowArray), std::nothrow);
	::operator delete(const_cast<void*>(verifiedNothrowAligned), std::align_val_t { 64 }, std::nothrow);
	::operator delete[](const_cast<void*>(verifiedNothrowAlignedArray), std::align_val_t { 64 }, std::nothrow);
	verifiedAllocation = verifiedAlignedAllocation = verifiedScalarAllocation = nullptr;
	verifiedNothrowScalar = verifiedNothrowArray = verifiedNothrowAligned = verifiedNothrowAlignedArray = nullptr;
}
}

void* operator new(std::size_t size) { return allocate(size); }
void* operator new[](std::size_t size) { return allocate(size); }
void* operator new(std::size_t size, std::align_val_t alignment) { return allocateAligned(size, alignment); }
void* operator new[](std::size_t size, std::align_val_t alignment) { return allocateAligned(size, alignment); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept { return allocateNoThrow(size); }
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept { return allocateNoThrow(size); }
void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept { return allocateAlignedNoThrow(size, alignment); }
void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept { return allocateAlignedNoThrow(size, alignment); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete(void* memory, std::align_val_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::align_val_t) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t, std::align_val_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t, std::align_val_t) noexcept { std::free(memory); }
void operator delete(void* memory, const std::nothrow_t&) noexcept { std::free(memory); }
void operator delete[](void* memory, const std::nothrow_t&) noexcept { std::free(memory); }
void operator delete(void* memory, std::align_val_t, const std::nothrow_t&) noexcept { std::free(memory); }
void operator delete[](void* memory, std::align_val_t, const std::nothrow_t&) noexcept { std::free(memory); }

namespace vekt::audio_lab::callback_allocation_probe
{
void begin() noexcept
{
	allocations = 0;
	measuring = true;
}

std::uint64_t end() noexcept
{
	measuring = false;
	return allocations;
}

bool verify() noexcept
{
	begin();
	try
	{
		verifiedAllocation = new char[33];
		verifiedAlignedAllocation = ::operator new(33, std::align_val_t { 64 });
		verifiedScalarAllocation = new char;
		verifiedNothrowScalar = ::operator new(33, std::nothrow);
		verifiedNothrowArray = ::operator new[](33, std::nothrow);
		verifiedNothrowAligned = ::operator new(33, std::align_val_t { 64 }, std::nothrow);
		verifiedNothrowAlignedArray = ::operator new[](33, std::align_val_t { 64 }, std::nothrow);
	}
	catch (...)
	{
		(void)end();
		releaseVerificationAllocations();
		return false;
	}
	const auto count = end();
	const auto allAllocated = verifiedNothrowScalar != nullptr && verifiedNothrowArray != nullptr
		&& verifiedNothrowAligned != nullptr && verifiedNothrowAlignedArray != nullptr;
	releaseVerificationAllocations();
	return count == 7 && allAllocated;
}
}