// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>

namespace Vulkan::Detail {

// The command thread writes constant ring snapshots before publishing the
// draw/dispatch packet. Flushing once after ALL writes of that packet is
// equivalent to flushing each snapshot (VkBuffer visibility precedes the
// recording thread's descriptor reads) and reduces costly VMA flushes on
// non-coherent host mappings. Monotonic ring positions can cross a wrap;
// convert them to at most two ranges in the underlying Vulkan allocation.
template<class Flush>
inline void ForRingFlushSegments(std::uint64_t begin, std::uint64_t end,
                                 std::uint64_t capacity, Flush&& flush) {
    if (end <= begin || !capacity) return;
    const std::uint64_t span=end-begin;
    if (span >= capacity) {
        flush(0, capacity);
        return;
    }
    const std::uint64_t from=begin%capacity;
    if (span <= capacity-from) {
        flush(from, span);
    } else {
        flush(from, capacity-from);
        flush(0, span-(capacity-from));
    }
}

} // namespace Vulkan::Detail
