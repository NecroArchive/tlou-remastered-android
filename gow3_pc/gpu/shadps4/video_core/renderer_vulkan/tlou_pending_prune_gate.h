// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

namespace Vulkan::Detail {

// Stage A tracks a CONSERVATIVE minimum of the packet positions still present
// in pending_writes. Range merging can replace an old position with a newer
// one, leaving the minimum stale/too small; that only causes extra pruning,
// never a false skip. Once a prune actually removes an entry, Rebuild() finds
// the exact minimum of the remaining vector.
//
// PendingUncommitted == UINT64_MAX needs no special handling: a write whose
// packet has not committed cannot already have reached Stage B. PostDraw
// calls Note() again when it replaces that sentinel with the committed head.
class PendingPruneGate {
public:
    void Note(std::uint64_t position) noexcept {
        oldest_ = std::min(oldest_, position);
        // An uncommitted packet is recorded as UINT64_MAX. Stage A never
        // prunes while such indices exist, and Commit() calls Note() again
        // with their actual packet end. Ignore only that temporary sentinel.
        if (position != std::numeric_limits<std::uint64_t>::max()) {
            newest_ = std::max(newest_, position);
            have_committed_ = true;
        }
    }

    // The 30k+ guest-ring prune passes every five seconds often retire the
    // ENTIRE short write set. An upper bound on completion positions lets us
    // clear it in O(1), instead of repeated erase/move/index/filter rebuilds.
    // Called only when there are no uncommitted indices left.
    [[nodiscard]] bool AllRetired(std::uint64_t completed) const noexcept {
        return have_committed_ && newest_ <= completed;
    }

    [[nodiscard]] bool MayRetire(std::uint64_t completed) const noexcept {
        return oldest_ <= completed;
    }

    template <class Write>
    void Rebuild(const std::vector<Write>& writes) noexcept {
        oldest_ = std::numeric_limits<std::uint64_t>::max();
        newest_ = 0;
        have_committed_ = false;
        for (const auto& write : writes) {
            Note(write.position);
        }
    }

    [[nodiscard]] std::uint64_t Oldest() const noexcept { return oldest_; }
    [[nodiscard]] std::uint64_t Newest() const noexcept { return newest_; }

private:
    std::uint64_t oldest_ = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t newest_ = 0;
    bool have_committed_ = false;
};

} // namespace Vulkan::Detail
