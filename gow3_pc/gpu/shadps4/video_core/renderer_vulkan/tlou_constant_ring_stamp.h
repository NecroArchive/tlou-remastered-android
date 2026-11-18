// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>

namespace Vulkan::Detail {

// One constant ring consumer (Stage B). A snapshot is only safe to retire
// after the GPU signals the submission tick that contained its draw.
//
// IMPORTANT: even if the producer position does not advance because the
// ring filled, a *new tick* must publish the last position from the prior
// submission. Returning solely on end == pending_end permanently wedges a
// full ring: later Stage-A allocations fail, so end never advances again.
//
// Publish may decline to queue a stamp when its bounded FIFO is full. It
// updates last_published only after successfully enqueueing the stamp; the
// next tick can retry without incorrectly claiming successful publication.
struct ConstantRingStampProgress {
    std::uint64_t pending_end = 0;
    std::uint64_t pending_tick = 0;
    std::uint64_t last_published = 0;

    template <class Publish>
    void Stamp(std::uint64_t end, std::uint64_t tick, Publish&& publish) {
        if (end == pending_end && tick == pending_tick) return;
        if (tick != pending_tick && pending_end != last_published) {
            publish(pending_end, pending_tick);
        }
        pending_end = end;
        pending_tick = tick;
    }
};

} // namespace Vulkan::Detail
