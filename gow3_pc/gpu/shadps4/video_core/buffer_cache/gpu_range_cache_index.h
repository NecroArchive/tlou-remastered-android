// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace VideoCore::Detail {

// Conservative monotonic bitmap of 64 KiB guest windows that the GPU has
// ever written. Never clears bits. A cached *identical* interval can skip this
// call: its first insertion marked all of these windows. Kept as a helper so
// the readback regression test exercises exactly the production bit writes.
inline void MarkGpuWriteWindows(std::atomic<std::uint64_t>* words,
                                std::uint64_t window_bits,
                                std::uint64_t address,
                                std::uint64_t size) noexcept {
    if (!size) return;
    const std::uint64_t first = address >> 16;
    if (first >= window_bits) return;
    const std::uint64_t end = size - 1 > UINT64_MAX - address
        ? UINT64_MAX : address + size - 1;
    const std::uint64_t last = std::min(end >> 16, window_bits - 1);
    for (std::uint64_t w = first; w <= last; ++w) {
        auto& word = words[w >> 6];
        const std::uint64_t bit = std::uint64_t{1} << (w & 63);
        if (!(word.load(std::memory_order_relaxed) & bit)) {
            word.fetch_or(bit, std::memory_order_relaxed);
        }
    }
}

// Exact candidate index for a small direct-mapped range cache. An overlapping
// pair must touch at least one common WindowShift-sized guest address window;
// the common window always hashes to the same bucket. Hash collisions only
// cause extra candidate checks, never a missed cache invalidation.
template <std::size_t Slots = 512, std::size_t Buckets = 64, unsigned WindowShift = 20>
class GpuRangeCacheIndex {
    static_assert(Slots && Slots % 64 == 0);
    static_assert(Buckets == 64, "the per-slot membership fits a uint64_t");
    static_assert(WindowShift > 0 && WindowShift < 63);
    using Words = std::array<std::uint64_t, Slots / 64>;

public:
    static std::uint64_t BucketMask(std::uint64_t address, std::uint64_t size) noexcept {
        if (size == 0) return 0;
        const std::uint64_t first = address >> WindowShift;
        const std::uint64_t last = (size - 1 > UINT64_MAX - address)
            ? (UINT64_MAX >> WindowShift) : ((address + size - 1) >> WindowShift);
        if (last - first >= Buckets) return UINT64_MAX;
        std::uint64_t mask = 0;
        for (std::uint64_t window = first; window <= last; ++window) {
            const auto bucket = (window ^ (window >> 6)) & (Buckets - 1);
            mask |= std::uint64_t{1} << bucket;
        }
        return mask;
    }

    void Clear(std::size_t slot) noexcept {
        const auto bit = std::uint64_t{1} << (slot & 63);
        std::uint64_t mask = members[slot];
        while (mask) {
            const auto bucket = std::countr_zero(mask);
            bucket_slots[bucket][slot >> 6] &= ~bit;
            mask &= mask - 1;
        }
        members[slot] = 0;
    }

    void Replace(std::size_t slot, std::uint64_t address, std::uint64_t size) noexcept {
        Clear(slot);
        const auto mask = BucketMask(address, size);
        members[slot] = mask;
        const auto bit = std::uint64_t{1} << (slot & 63);
        for (std::uint64_t bits = mask; bits; bits &= bits - 1) {
            const auto bucket = std::countr_zero(bits);
            bucket_slots[bucket][slot >> 6] |= bit;
        }
    }

    template <class Fn>
    void ForCandidates(std::uint64_t address, std::uint64_t size, Fn&& fn) const {
        Words candidates{};
        const auto mask = BucketMask(address, size);
        for (std::uint64_t bits = mask; bits; bits &= bits - 1) {
            const auto bucket = std::countr_zero(bits);
            for (std::size_t word = 0; word < candidates.size(); ++word) {
                candidates[word] |= bucket_slots[bucket][word];
            }
        }
        for (std::size_t word = 0; word < candidates.size(); ++word) {
            auto bits = candidates[word];
            while (bits) {
                fn(word * 64 + std::countr_zero(bits));
                bits &= bits - 1;
            }
        }
    }

private:
    std::array<Words, Buckets> bucket_slots{};
    std::array<std::uint64_t, Slots> members{};
};

} // namespace VideoCore::Detail
