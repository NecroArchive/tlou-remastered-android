// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstdint>

namespace Vulkan::Detail {
// 4096-granule cyclic pending-write bloom, preserving legacy wrap semantics.
// The common 1-5 granule case uses the original bit loop; large intervals
// operate on 64-bit words. Never a replacement for the exact overlap test.
using PendingFilterWords = std::array<std::uint64_t, 64>;
constexpr std::uint64_t PendingFilterBitCount = 4096;

inline void SetPendingWordSegment(PendingFilterWords& bits, unsigned first, unsigned end) {
    if (first >= end) return;
    const unsigned begin_word = first >> 6, last_word = (end - 1) >> 6;
    const std::uint64_t low = ~std::uint64_t{0} << (first & 63);
    const std::uint64_t high = ~std::uint64_t{0} >> (63 - ((end - 1) & 63));
    if (begin_word == last_word) { bits[begin_word] |= low & high; return; }
    bits[begin_word] |= low;
    for (unsigned w = begin_word + 1; w < last_word; ++w) bits[w] = ~std::uint64_t{0};
    bits[last_word] |= high;
}
inline bool AnyPendingWordSegment(const PendingFilterWords& bits, unsigned first, unsigned end) {
    if (first >= end) return false;
    const unsigned begin_word = first >> 6, last_word = (end - 1) >> 6;
    const std::uint64_t low = ~std::uint64_t{0} << (first & 63);
    const std::uint64_t high = ~std::uint64_t{0} >> (63 - ((end - 1) & 63));
    if (begin_word == last_word) return (bits[begin_word] & (low & high)) != 0;
    if (bits[begin_word] & low) return true;
    for (unsigned w = begin_word + 1; w < last_word; ++w) if (bits[w]) return true;
    return (bits[last_word] & high) != 0;
}
inline void MarkPendingRange(PendingFilterWords& bits, std::uint64_t first, std::uint64_t last) {
    if (last - first >= PendingFilterBitCount) { bits.fill(~std::uint64_t{0}); return; }
    if (last - first <= 4) {
        for (auto granule = first; granule <= last; ++granule) {
            const unsigned bit = unsigned(granule & (PendingFilterBitCount - 1));
            bits[bit >> 6] |= std::uint64_t{1} << (bit & 63);
        }
        return;
    }
    const unsigned offset = unsigned(first & (PendingFilterBitCount - 1));
    const unsigned stop = offset + unsigned(last - first + 1);
    if (stop <= PendingFilterBitCount) SetPendingWordSegment(bits, offset, stop);
    else { SetPendingWordSegment(bits, offset, PendingFilterBitCount); SetPendingWordSegment(bits, 0, stop - PendingFilterBitCount); }
}
inline bool HasPendingRange(const PendingFilterWords& bits, std::uint64_t first, std::uint64_t last) {
    if (last - first >= PendingFilterBitCount) return true;
    if (last - first <= 4) {
        for (auto granule = first; granule <= last; ++granule) {
            const unsigned bit = unsigned(granule & (PendingFilterBitCount - 1));
            if (bits[bit >> 6] & (std::uint64_t{1} << (bit & 63))) return true;
        }
        return false;
    }
    const unsigned offset = unsigned(first & (PendingFilterBitCount - 1));
    const unsigned stop = offset + unsigned(last - first + 1);
    return stop <= PendingFilterBitCount ? AnyPendingWordSegment(bits, offset, stop)
        : AnyPendingWordSegment(bits, offset, PendingFilterBitCount) || AnyPendingWordSegment(bits, 0, stop - PendingFilterBitCount);
}
} // namespace Vulkan::Detail
