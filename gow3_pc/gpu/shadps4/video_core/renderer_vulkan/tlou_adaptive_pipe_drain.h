// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <chrono>
#include <cstdint>
#include <utility>
namespace Vulkan::Detail {

// The Android log 24 -> 25 regression coincided with changing DrawPipe::Drain
// from spin/yield to futex parking after only ~4096 CPU relax iterations.
// Stage A usually waits 10-30 us, so blocking/waking costs as much as the
// work it waits for. Preserve the fast spin/yield handoff for short waits;
// atomically park only after the producer has made no progress for 250 us.
// The consumer's atomic progress notification and precise completion checks
// are unchanged. No guest readback or memory dependency is skipped.
template <class Ready, class Pause, class Yield, class Park>
inline void AdaptivePipeDrain(Ready&& ready, Pause&& pause, Yield&& yield,
                              Park&& park,
                              std::chrono::microseconds park_after =
                                  std::chrono::microseconds{250}) {
    std::uint32_t spins = 0;
    std::chrono::steady_clock::time_point after_spin{};
    while (!ready()) {
        if (spins++ < 4096) {
            pause();
            continue;
        }
        if (after_spin == std::chrono::steady_clock::time_point{}) {
            after_spin = std::chrono::steady_clock::now();
        } else if ((spins & 255u) == 0u &&
                   std::chrono::steady_clock::now() - after_spin >= park_after) {
            park(); // rechecks atomically and sleeps until exact Stage-B progress
            return;
        }
        yield();
    }
}
} // namespace Vulkan::Detail
