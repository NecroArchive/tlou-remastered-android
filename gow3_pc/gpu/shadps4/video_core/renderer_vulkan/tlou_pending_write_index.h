// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>
#include "video_core/buffer_cache/gpu_range_cache_index.h"

namespace Vulkan::Detail {

// Preserve the original cyclic-first overlap selection. A hash collision can
// only add extra candidate checks; no interval is omitted. Adjacent writes
// count as overlapping in NotePendingGpuWrite, so the indexed end is inclusive.
// Stage A is the sole owner; do not use this on the recording thread.
template <class Write>
[[nodiscard]] inline bool PendingWriteHintOverlaps(const std::vector<Write>& writes,
              std::size_t hint, std::uint64_t begin, std::uint64_t end) noexcept {
    return hint < writes.size() && begin <= writes[hint].end && writes[hint].begin <= end;
}

template <class Write>
[[nodiscard]] inline bool PendingWriteAlreadyCovered(const Write& write,
              std::uint64_t begin, std::uint64_t end) noexcept {
    return begin >= write.begin && end <= write.end;
}

template <class Write, std::size_t Capacity = 2048>
class PendingWriteIndex {
    static_assert(Capacity % 64 == 0);
    VideoCore::Detail::GpuRangeCacheIndex<Capacity, 64, 16> index{};
    static std::uint64_t Span(std::uint64_t begin, std::uint64_t end) noexcept {
        if (end < begin) return 0;
        const auto delta = end - begin;
        return delta == UINT64_MAX ? UINT64_MAX : delta + 1;
    }

public:
    static constexpr std::size_t MinQuerySize = 32;
    static constexpr std::size_t MaxQuerySize = Capacity;

    [[nodiscard]] bool CanQuery(std::size_t count) const noexcept {
        return count >= MinQuerySize && count <= Capacity;
    }

    static constexpr std::size_t MinLiveQuerySize = 128;
    [[nodiscard]] bool CanQueryLive(std::size_t count) const noexcept {
        return count >= MinLiveQuerySize && count <= Capacity;
    }

    // The read-only constant ring can skip a guest snapshot only if no
    // overlapping output from Stage B remains unretired. Unlike merging
    // output writes (inclusive adjacency), a read overlap is STRICT and the
    // caller's reached-position predicate is authoritative. Bucket collisions
    // are filtered against exact endpoints and never cause a false miss.
    template <class IsLive>
    [[nodiscard]] bool AnyLiveOverlap(const std::vector<Write>& writes,
                 std::uint64_t begin, std::uint64_t end, IsLive&& live) const {
        if (!CanQueryLive(writes.size()) || end <= begin) return false;
        bool found = false;
        index.ForCandidates(begin, Span(begin, end), [&](std::size_t slot) {
            if (found || slot >= writes.size()) return;
            const Write& write = writes[slot];
            if (begin < write.end && write.begin < end && live(write)) {
                found = true;
            }
        });
        return found;
    }

    void Set(std::size_t slot, const Write& write) noexcept {
        if (slot >= Capacity) return;
        index.Replace(slot, write.begin, Span(write.begin, write.end));
    }

    // Required when std::erase_if shifts vector indices after Stage B completes.
    // Do not rebuild on a pruning pass that erased no elements.
    void Rebuild(const std::vector<Write>& writes) noexcept {
        index = {};
        for (std::size_t i = 0; i < std::min(writes.size(), Capacity); ++i) {
            Set(i, writes[i]);
        }
    }

    // The old scan starts at hint and wraps. Choosing just any overlapping
    // candidate changes which pending write's submission position is advanced.
    // Select the same first-overlap in cyclic order, even for merged intervals.
    [[nodiscard]] std::optional<std::uint32_t> FirstOverlap(
        const std::vector<Write>& writes, std::uint32_t hint,
        std::uint64_t begin, std::uint64_t end) const {
        const auto count = static_cast<std::uint32_t>(writes.size());
        if (!CanQuery(count) || end < begin) return std::nullopt;
        const auto first = hint < count ? hint : 0u;
        std::uint32_t best = count, best_distance = count;
        index.ForCandidates(begin, Span(begin, end), [&](std::size_t slot) {
            if (slot >= count) return;
            const auto& write = writes[slot];
            if (begin > write.end || write.begin > end) return;
            const auto candidate = static_cast<std::uint32_t>(slot);
            const auto distance =
                candidate >= first ? candidate - first : candidate + count - first;
            if (distance < best_distance) {
                best = candidate;
                best_distance = distance;
            }
        });
        return best < count ? std::optional<std::uint32_t>{best} : std::nullopt;
    }
};
} // namespace Vulkan::Detail
