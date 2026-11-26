// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <atomic>
#include <cstdint>
#include <thread>

namespace Vulkan::Detail {

// Wait for the exact recorder-copy sequence requested by the guest-visible
// fence. The recording chunk is already published by KickRecording(true).
// Short completions use a few yields; long waits park instead of burning a
// whole core while the Vulkan recorder or other game workers need CPU time.
// Every release-store that advances completed must notify_all().
inline void WaitForHostCopySequence(std::atomic<std::uint64_t>& completed,
                                    std::uint64_t target) {
    // Android's scheduler yield is relatively costly on the measured
    // 5-10 us handoffs. A bounded local poll gives the already-running
    // recorder time to publish its sequence before we enter the kernel.
    // Never spin indefinitely or weaken the acquire on guest-copy data.
#if defined(__ANDROID__) && defined(__aarch64__)
    for (unsigned i = 0; i < 128; ++i) {
        if (completed.load(std::memory_order_acquire) >= target) return;
        asm volatile("yield" ::: "memory");
    }
#else
    for (unsigned i = 0; i < 4; ++i) {
        if (completed.load(std::memory_order_acquire) >= target) return;
        std::this_thread::yield();
    }
#endif
    for (;;) {
        const auto observed = completed.load(std::memory_order_acquire);
        if (observed >= target) return;
        completed.wait(observed, std::memory_order_acquire);
    }
}

} // namespace Vulkan::Detail
