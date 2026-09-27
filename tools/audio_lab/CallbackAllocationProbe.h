#pragma once

#include <cstdint>

namespace vekt::audio_lab::callback_allocation_probe
{
// Counts ordinary, aligned, and nothrow C++ new/new[] on the calling thread.
// Direct malloc and work dispatched to other threads are outside this probe.
void begin() noexcept;
[[nodiscard]] std::uint64_t end() noexcept;
[[nodiscard]] bool verify() noexcept;
}