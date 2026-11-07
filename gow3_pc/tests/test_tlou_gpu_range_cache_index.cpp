// SPDX-License-Identifier: GPL-2.0-or-later
#include "video_core/buffer_cache/gpu_range_cache_index.h"
#include <array>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string_view>
#include <vector>

using Index = VideoCore::Detail::GpuRangeCacheIndex<>;
constexpr size_t Slots = 512;
struct Range { uint64_t addr = 0, size = 0; };
bool overlap(Range x, Range y) {
    return x.size && y.size && x.addr < y.addr + y.size && y.addr < x.addr + x.size;
}
struct Old {
    std::array<Range, Slots> entries{};
    uint64_t touches = 0;
    void Add(size_t slot, Range r) { entries[slot] = r; }
    void Sub(Range r) {
        for (auto& e : entries) {
            ++touches;
            if (overlap(e, r)) e = {};
        }
    }
};
struct New {
    std::array<Range, Slots> entries{};
    Index index;
    uint64_t touches = 0;
    void Add(size_t slot, Range r) {
        index.Replace(slot, r.addr, r.size);
        entries[slot] = r;
    }
    void Sub(Range r) {
        index.ForCandidates(r.addr, r.size, [&](size_t slot) {
            ++touches;
            if (overlap(entries[slot], r)) {
                entries[slot] = {};
                index.Clear(slot);
            }
        });
    }
};
void correctness() {
    std::mt19937_64 rng(1234);
    Old old;
    New neu;
    for (size_t i = 0; i < 250000; i++) {
        const uint64_t a = (rng() % 8192) * 4096;
        const uint64_t size = 1 + rng() % 32768;
        const Range r{a, size};
        if ((rng() % 100) < 39) {
            old.Sub(r);
            neu.Sub(r);
        } else {
            const size_t slot = rng() % Slots;
            old.Add(slot, r);
            neu.Add(slot, r);
        }
        if ((i & 1023) == 0) {
            for (size_t slot = 0; slot < Slots; slot++) {
                assert(old.entries[slot].addr == neu.entries[slot].addr);
                assert(old.entries[slot].size == neu.entries[slot].size);
            }
        }
    }
    for (size_t slot = 0; slot < Slots; slot++) {
        assert(old.entries[slot].addr == neu.entries[slot].addr);
        assert(old.entries[slot].size == neu.entries[slot].size);
    }
    assert(Index::BucketMask(0, 0) == 0);
    assert(Index::BucketMask(0, UINT64_MAX) == UINT64_MAX);
    assert(Index::BucketMask(UINT64_MAX - 4, 20));
    std::printf("GPU range index: 250000 randomized operations, no mismatches\n");
}
// Host-only benchmark: production index vs previous 512-slot invalidation.
// Its speedup is NOT a claimed Android FPS improvement.
void benchmark(size_t reuse_percent) {
    std::mt19937_64 rng(9342 + reuse_percent);
    struct Event { bool subtract; uint16_t slot; Range r; };
    std::vector<Event> events;
    events.reserve(420000);
    std::array<Range, Slots> hot{};
    for (size_t i = 0; i < 400000; i++) {
        const uint16_t slot = rng() % Slots;
        Range r;
        if (rng() % 100 < reuse_percent && hot[slot].size) r = hot[slot];
        else {
            r = {((rng() % 4096) * 4096) + 0x10000000, 64 + 64 * (rng() % 8)};
            hot[slot] = r;
        }
        events.push_back({false, slot, r});
        if (i % 25 == 0)
            events.push_back({true, 0, {0x10000000 + (rng() % 4096) * 4096,
                                         32 + 64 * (rng() % 5)}});
    }
    volatile uint64_t sink = 0;
    const auto run = [&](bool indexed) {
        using Clock = std::chrono::steady_clock;
        const auto start = Clock::now();
        if (indexed) {
            New n;
            for (const auto& e : events) {
                if (e.subtract) n.Sub(e.r);
                else if (n.entries[e.slot].addr != e.r.addr ||
                         n.entries[e.slot].size != e.r.size) n.Add(e.slot, e.r);
            }
            sink = sink + n.touches;
        } else {
            Old n;
            for (const auto& e : events) {
                if (e.subtract) n.Sub(e.r);
                else if (n.entries[e.slot].addr != e.r.addr ||
                         n.entries[e.slot].size != e.r.size) n.Add(e.slot, e.r);
            }
            sink = sink + n.touches;
        }
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    };
    std::vector<double> before, after;
    for (int i = 0; i < 9; i++) {
        if (i & 1) {
            after.push_back(run(true));
            before.push_back(run(false));
        } else {
            before.push_back(run(false));
            after.push_back(run(true));
        }
    }
    std::sort(before.begin(), before.end());
    std::sort(after.begin(), after.end());
    std::printf("GPU range index: reuse=%zu%%, 400k adds/16k invalidations "
                "old=%.3fms indexed=%.3fms ratio=%.3f\n",
                reuse_percent, before[4], after[4], after[4] / before[4]);
    (void)sink;
}
int main(int argc, char** argv) {
    correctness();
    if (argc == 1) {
        benchmark(90);
        benchmark(50);
        benchmark(10);
        return 0;
    }
    return argc == 2 && std::string_view(argv[1]) == "--correctness-only" ? 0 : 2;
}
