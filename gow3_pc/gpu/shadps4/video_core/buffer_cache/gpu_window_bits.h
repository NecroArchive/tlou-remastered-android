// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <atomic>
#include <cstdint>

namespace VideoCore::Detail {
// Inclusive window indices, already clamped to the allocation by the caller.
// Sticky bits: group a range's updates by word, retaining atomic fetch_or so
// concurrent writers of disjoint bits in the same word cannot lose each other.
template<class AtomicWord>
void MarkGpuWindowBits(AtomicWord* words, std::uint64_t first, std::uint64_t last) {
    if (first > last) return;
    if (first == last) {
        auto& word = words[first >> 6];
        const auto bit = std::uint64_t{1} << (first & 63);
        if (!(word.load(std::memory_order_relaxed) & bit)) word.fetch_or(bit, std::memory_order_relaxed);
        return;
    }
    const auto first_word = first >> 6;
    const auto last_word = last >> 6;
    for (auto index = first_word; index <= last_word; ++index) {
        auto mask = ~std::uint64_t{0};
        if (index == first_word) mask &= ~std::uint64_t{0} << (first & 63);
        if (index == last_word) mask &= ~std::uint64_t{0} >> (63 - (last & 63));
        auto& word = words[index];
        if ((word.load(std::memory_order_relaxed) & mask) != mask) {
            word.fetch_or(mask, std::memory_order_relaxed);
        }
    }
}
} // namespace VideoCore::Detail
