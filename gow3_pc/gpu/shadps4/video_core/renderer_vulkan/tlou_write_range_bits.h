// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace Vulkan::Detail {

// A 4,096-bit modulo-address filter; it only returns "maybe" and must never
// introduce a false negative. The caller still tests the exact intervals.
// A range is already expressed as inclusive 64-KiB granule indices.
template<std::size_t Words>
void SetPendingRangeBits(std::array<std::uint64_t, Words>& bits,
                         std::uint64_t first, std::uint64_t last) noexcept {
    constexpr std::size_t Bits = Words * 64;
    static_assert(Words != 0 && (Bits & (Bits - 1)) == 0);
    if (first == last) {
        const std::uint64_t pos = first & (Bits - 1);
        bits[pos >> 6] |= std::uint64_t{1} << (pos & 63);
        return;
    }
    if (last - first >= Bits) {
        bits.fill(~std::uint64_t{0});
        return;
    }
    for (std::uint64_t current = first; current <= last;) {
        const std::uint64_t pos = current & (Bits - 1);
        const unsigned off = static_cast<unsigned>(pos & 63);
        const unsigned count = static_cast<unsigned>(
            std::min<std::uint64_t>(last - current + 1, 64 - off));
        const auto mask = count == 64 ? ~std::uint64_t{0}
                                      : ((std::uint64_t{1} << count) - 1) << off;
        bits[pos >> 6] |= mask;
        current += count;
    }
}

template<std::size_t Words>
bool PendingRangeBitsMayHit(const std::array<std::uint64_t, Words>& bits,
                            std::uint64_t first, std::uint64_t last) noexcept {
    constexpr std::size_t Bits = Words * 64;
    static_assert(Words != 0 && (Bits & (Bits - 1)) == 0);
    if (first == last) {
        const std::uint64_t pos = first & (Bits - 1);
        return (bits[pos >> 6] & (std::uint64_t{1} << (pos & 63))) != 0;
    }
    if (last - first >= Bits) {
        return true;
    }
    for (std::uint64_t current = first; current <= last;) {
        const std::uint64_t pos = current & (Bits - 1);
        const unsigned off = static_cast<unsigned>(pos & 63);
        const unsigned count = static_cast<unsigned>(
            std::min<std::uint64_t>(last - current + 1, 64 - off));
        const auto mask = count == 64 ? ~std::uint64_t{0}
                                      : ((std::uint64_t{1} << count) - 1) << off;
        if (bits[pos >> 6] & mask) {
            return true;
        }
        current += count;
    }
    return false;
}

// Monotonic atomic "ever written" bitmap. Retains the original relaxed
// publication semantics; fetch_or only if some bits of this word are absent.
// Concurrent writers cannot erase each other's bits.
inline void MarkGpuWriteWindowBits(std::atomic<std::uint64_t>* bits,
                                   std::uint64_t first, std::uint64_t last) noexcept {
    if (first == last) {
        auto& word = bits[first >> 6];
        const auto bit = std::uint64_t{1} << (first & 63);
        if (!(word.load(std::memory_order_relaxed) & bit))
            word.fetch_or(bit, std::memory_order_relaxed);
        return;
    }
    for (std::uint64_t current = first; current <= last;) {
        const unsigned off = static_cast<unsigned>(current & 63);
        const unsigned count = static_cast<unsigned>(
            std::min<std::uint64_t>(last - current + 1, 64 - off));
        const auto mask = count == 64 ? ~std::uint64_t{0}
                                      : ((std::uint64_t{1} << count) - 1) << off;
        auto& word = bits[current >> 6];
        if ((word.load(std::memory_order_relaxed) & mask) != mask) {
            word.fetch_or(mask, std::memory_order_relaxed);
        }
        current += count;
    }
}

} // namespace Vulkan::Detail
