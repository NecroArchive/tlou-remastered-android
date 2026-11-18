// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <atomic>
#include <cstdint>
#include <thread>

namespace Vulkan::Detail {

// Guest-visible writes must wait for all deferred signals issued BEFORE that
// write. They do not need to drain later, unrelated host-copy pool jobs.
// The old WaitAsync() call in each polling iteration waited for the ENTIRE
// pool to go idle, including copies submitted after the required signal.
//
// A few yields preserve low overhead for near-instant completions; the atomic
// wait is only used for sustained stalls. Completion must notify_all().
// Without parallel copy workers, the caller must keep helping the queue to
// prevent deadlock.
template <class HelpCopies>
void WaitForDeferredSignal(std::atomic<std::uint64_t>& completed,
                           std::uint64_t target,
                           bool parallel_copy_workers,
                           HelpCopies&& help_copies) {
    unsigned short_spins = 0;
    while (completed.load(std::memory_order_acquire) < target) {
        if (!parallel_copy_workers) {
            help_copies();
            std::this_thread::yield();
        } else if (short_spins++ < 64) {
            std::this_thread::yield();
        } else {
            const auto observed = completed.load(std::memory_order_acquire);
            if (observed < target) {
                completed.wait(observed, std::memory_order_acquire);
            }
        }
    }
}

} // namespace Vulkan::Detail
