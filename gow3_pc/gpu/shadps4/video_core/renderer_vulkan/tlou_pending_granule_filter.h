// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace Vulkan::Detail {

// The pending-write filter hashes 64 KiB granules into 4096 bits. Publish the
// exact same bits as the previous per-granule loop, but cover each contiguous
// run of up to 64 bits with one OR. Small ranges remain one word operation.
// Stage A is the sole writer; the filter is not a synchronization primitive.
template <std::size_t Bits, unsigned GranuleBits>
inline void MarkPendingGranules(std::array<std::uint64_t, Bits / 64>& filter,
                                std::uint64_t begin, std::uint64_t end) noexcept {
    static_assert(Bits >= 64 && Bits % 64 == 0 && (Bits & (Bits - 1)) == 0);
    if (end <= begin) {
        return;
    }
    const std::uint64_t first = begin >> GranuleBits;
    const std::uint64_t last = (end - 1) >> GranuleBits;
    if (last - first >= Bits) {
        filter.fill(~std::uint64_t{0});
        return;
    }
    if (first == last) {
        const unsigned bit = static_cast<unsigned>(first & (Bits - 1));
        filter[bit >> 6] |= std::uint64_t{1} << (bit & 63);
        return;
    }
    std::uint64_t remaining = last - first + 1;
    std::uint64_t at = first;
    while (remaining) {
        const unsigned bit = static_cast<unsigned>(at & (Bits - 1));
        const unsigned shift = bit & 63;
        const unsigned count =
            static_cast<unsigned>(std::min<std::uint64_t>(remaining, 64 - shift));
        // count can be 64; never shift by 64 (undefined behavior).
        const std::uint64_t mask = (~std::uint64_t{0} >> (64 - count)) << shift;
        filter[bit >> 6] |= mask;
        at += count;
        remaining -= count;
    }
}

// The circular pending-write bloom filter can be queried 64 granules per
// mask instead of one bit at a time. Exactly the same collision semantics.
template<std::size_t Bits, unsigned GranuleBits>
[[nodiscard]] inline bool AnyPendingGranules(
    const std::array<std::uint64_t, Bits / 64>& filter,
    std::uint64_t begin, std::uint64_t end) noexcept {
    static_assert(Bits >= 64 && Bits % 64 == 0 && (Bits & (Bits-1)) == 0);
    if (end <= begin) return false;
    const auto first=begin>>GranuleBits, last=(end-1)>>GranuleBits;
    if (last-first>=Bits) return true;
    auto at=first, left=last-first+1;
    while(left) {
        const unsigned bit=static_cast<unsigned>(at & (Bits-1));
        const unsigned shift=bit & 63;
        const unsigned count=static_cast<unsigned>(std::min<std::uint64_t>(left,64-shift));
        const std::uint64_t mask=(~std::uint64_t{0}>>(64-count))<<shift;
        if(filter[bit>>6] & mask) return true;
        left-=count;at+=count;
    }
    return false;
}

} // namespace Vulkan::Detail
