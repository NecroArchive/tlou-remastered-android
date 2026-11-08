// SPDX-License-Identifier: GPL-2.0-or-later
// Optional CPU experiment: publish the exact same pending-write granule bits
// as the per-granule loop, but fill long contiguous stretches one word at a
// time. Do not merge/omit GPU writes, alter snapshot timing or touch readbacks.
#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
namespace Vulkan::Detail {
template<unsigned GranuleBits, std::size_t Words>
inline void MarkPendingBloomWords(std::array<std::uint64_t, Words>& filter,
                                  std::uint64_t begin, std::uint64_t end) {
    static_assert(Words && (Words * 64 & (Words * 64 - 1)) == 0);
    const std::uint64_t first = begin >> GranuleBits;
    const std::uint64_t last = (end - 1) >> GranuleBits;
    constexpr std::size_t FilterBits = Words * 64;
    if (last - first >= FilterBits) {
        filter.fill(~std::uint64_t{0});
        return;
    }
    // The vast majority of reads/writes span at most 1-2 64 KiB granules.
    // Preserve the original tiny-range loop to avoid a regression.
    if (last - first < 8) {
        for (std::uint64_t g = first; g <= last; ++g) {
            const std::uint32_t bit = std::uint32_t(g % FilterBits);
            filter[bit / 64] |= std::uint64_t{1} << (bit % 64);
        }
        return;
    }
    std::uint64_t g = first, remaining = last - first + 1;
    while (remaining) {
        const std::uint32_t bit = std::uint32_t(g % FilterBits);
        const unsigned shift = bit & 63;
        const std::uint64_t n = std::min<std::uint64_t>(remaining, 64 - shift);
        const std::uint64_t mask = (n == 64 ? ~std::uint64_t{0}
                                                 : (std::uint64_t{1} << n) - 1) << shift;
        filter[bit / 64] |= mask;
        g += n;
        remaining -= n;
    }
}
} // namespace Vulkan::Detail
