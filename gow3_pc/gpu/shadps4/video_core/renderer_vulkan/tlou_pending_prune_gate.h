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
    }

    [[nodiscard]] bool MayRetire(std::uint64_t completed) const noexcept {
        return oldest_ <= completed;
    }

    template <class Write>
    void Rebuild(const std::vector<Write>& writes) noexcept {
        oldest_ = std::numeric_limits<std::uint64_t>::max();
        for (const auto& write : writes) {
            Note(write.position);
        }
    }

    [[nodiscard]] std::uint64_t Oldest() const noexcept { return oldest_; }

private:
    std::uint64_t oldest_ = std::numeric_limits<std::uint64_t>::max();
};

} // namespace Vulkan::Detail
