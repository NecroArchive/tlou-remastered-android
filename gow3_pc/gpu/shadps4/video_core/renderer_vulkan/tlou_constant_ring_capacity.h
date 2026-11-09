// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>

namespace Vulkan::Detail {

// Only TLOU's Android Stage-A constant ring uses this capacity policy.
// A 32 MiB ring repeatedly filled in gameplay on 12-GiB devices, sending
// 1.3-1.8 million snapshots / 5s through slower Stage B. Keep the old
// capacity on memory-constrained devices and limit higher-RAM devices to
// 128 MiB, without touching the 4.5-GiB guest memory pool.
inline constexpr std::uint32_t ConstantRingMiB(std::uint64_t physical_bytes,
                                                std::uint32_t forced_mib = 0) noexcept {
    if (forced_mib == 32 || forced_mib == 64 || forced_mib == 128) return forced_mib;
    constexpr std::uint64_t GiB = std::uint64_t{1} << 30;
    if (physical_bytes >= 10 * GiB) return 128;
    if (physical_bytes >= 6 * GiB) return 64;
    return 32;
}

// Mirrors the production allocator's exact alignment and physical wrap.
// Invalid inputs are a programming error: capacity is always a multiple
// of page alignment and allocations are <= 16 KiB.
inline constexpr std::uint64_t ConstantRingAlignAndWrap(
    std::uint64_t position, std::uint64_t size,
    std::uint64_t alignment, std::uint64_t capacity) noexcept {
    std::uint64_t at = (position + alignment - 1) & ~(alignment - 1);
    if (at % capacity + size > capacity) at += capacity - at % capacity;
    return at;
}
inline constexpr bool ConstantRingOverlapsInFlight(
    std::uint64_t at, std::uint64_t size,
    std::uint64_t retired, std::uint64_t capacity) noexcept {
    return at + size > capacity && at + size - capacity > retired;
}

} // namespace Vulkan::Detail
