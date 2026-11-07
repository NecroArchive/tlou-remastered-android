// SPDX-License-Identifier: GPL-2.0-or-later
#include "video_core/renderer_vulkan/tlou_pending_filter_wordspan.h"
#include <cassert>
#include <cstdio>
#include <random>

using u64 = std::uint64_t;
using Words = Vulkan::Detail::PendingFilterWords;

static void LegacySet(Words& bits, u64 first, u64 last) {
    if (last - first >= 4096) { bits.fill(~u64{0}); return; }
    for (u64 granule = first; granule <= last; ++granule) {
        const unsigned bit = unsigned(granule % 4096);
        bits[bit / 64] |= u64{1} << (bit % 64);
    }
}
static bool LegacyHit(const Words& bits, u64 first, u64 last) {
    if (last - first >= 4096) return true;
    for (u64 granule = first; granule <= last; ++granule) {
        const unsigned bit = unsigned(granule % 4096);
        if (bits[bit / 64] & (u64{1} << (bit % 64))) return true;
    }
    return false;
}
int main() {
    std::mt19937_64 rng{0x995fabc321ULL};
    for (unsigned run = 0; run < 200000; ++run) {
        Words old{}, fast{};
        for (unsigned i = 0; i < 20; ++i) {
            const u64 first = rng() % (4096 * 128);
            const u64 n = 1 + (rng() % 6100);
            const u64 last = first + n - 1;
            LegacySet(old, first, last);
            Vulkan::Detail::MarkPendingRange(fast, first, last);
            assert(old == fast);
            const u64 qfirst = rng() % (4096 * 128);
            const u64 qlast = qfirst + (rng() % 6100);
            assert(LegacyHit(old, qfirst, qlast) ==
                   Vulkan::Detail::HasPendingRange(fast, qfirst, qlast));
        }
    }
    std::puts("TLOU pending-filter: 4 million randomized updates and 4 million randomized overlap queries passed");
}
