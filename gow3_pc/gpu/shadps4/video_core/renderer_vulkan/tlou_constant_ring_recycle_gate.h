// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>

namespace Vulkan::Detail {

// Stage A's constant-ring allocations are often blocked while the GPU is
// catching up. A Vulkan timeline Refresh() on EVERY failed allocation makes
// CPU cost proportional to millions of transient failures, without helping
// if neither published stamps nor known GPU progress changed.
//
// Probe immediately on (1) the first failure, (2) newly published stamps,
// (3) GPU tick progress observed elsewhere, or (4) every 32 blocked
// allocation attempts even if both counters remain unchanged. The last case
// discovers GPU progress that is not yet reflected in the cached GPU tick.
// Skipped probes still use the existing safe Stage-B snapshot fallback.
class ConstantRingRecycleGate {
public:
    static constexpr unsigned MaxBlockedBeforeProbe = 32;
    bool ShouldProbe(std::uint64_t published_head, std::uint64_t known_gpu_tick) noexcept {
        if (!initialized || published_head != last_head ||
            known_gpu_tick != last_gpu_tick || ++blocked_since_probe >= MaxBlockedBeforeProbe) {
            initialized = true;
            blocked_since_probe = 0;
            last_head = published_head;
            last_gpu_tick = known_gpu_tick;
            return true;
        }
        return false;
    }
private:
    bool initialized = false;
    unsigned blocked_since_probe = 0;
    std::uint64_t last_head = 0, last_gpu_tick = 0;
};

} // namespace Vulkan::Detail
