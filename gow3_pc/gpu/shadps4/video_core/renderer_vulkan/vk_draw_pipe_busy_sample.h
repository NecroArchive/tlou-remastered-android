// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <atomic>
#include "common/rdtsc.h"

namespace Vulkan::Detail {

// GOW3_FRAME_STATS is an optional diagnostic. Recording a busy interval for
// every draw-pipe packet otherwise executes two serializing counter reads
// (ISB/CNTVCT on ARM64) and one shared atomic increment on every packet.
// Keep the exact legacy accounting when enabled; skip all three when disabled.
inline u64 BeginDrawPipeBusySample(bool enabled) noexcept {
    return enabled ? Common::FencedRDTSC() : 0;
}

inline void EndDrawPipeBusySample(bool enabled, u64 begin, std::atomic<u64>& cycles) noexcept {
    if (enabled) {
        cycles.fetch_add(Common::FencedRDTSC() - begin, std::memory_order_relaxed);
    }
}

} // namespace Vulkan::Detail
