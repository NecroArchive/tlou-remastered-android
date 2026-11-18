// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
namespace Vulkan::Detail {
// Performance counters only, kept on the Vulkan recording thread. Decisions
// about readbacks, fences, and buffer contents are unchanged by these stats.
struct TlouReadbackHotStats {
    std::uint64_t probes=0, accepted=0;
    bool Note(bool eligible) noexcept {
        accepted += eligible;
        return ((++probes & 511u) == 0);
    }
    std::uint64_t TakeAccepted() noexcept {
        const auto n=accepted;
        accepted=0;
        return n;
    }
};
}
