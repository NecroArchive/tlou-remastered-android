// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <limits>

namespace VideoCore::Detail {
// Protected readback pages must be watched once the buffer submission tick
// becomes older than the scheduler's current tick. Every new async readback
// captures CurrentTick at enqueue, therefore a readback appended during an
// unchanged tick is never already submitted. Only a tick transition can make
// any un-watched pending readback newly eligible.
//
// Do not gate GPU completion polling, CPU read-fault waits, or explicit
// ReleaseStuckPage/CompleteReadbacks(true): only the O(pending) watcher sweep.
class TlouReadbackWatchSweepGate {
public:
    [[nodiscard]] bool ShouldSweep(std::uint64_t current_tick) noexcept {
        if (current_tick == last_tick_) return false;
        last_tick_ = current_tick;
        return true;
    }
private:
    std::uint64_t last_tick_ = std::numeric_limits<std::uint64_t>::max();
};
} // namespace VideoCore::Detail
