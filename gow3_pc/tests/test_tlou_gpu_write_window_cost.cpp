// SPDX-License-Identifier: GPL-2.0-or-later
// Bit-for-bit randomized equivalence: the old per-add bitmap scan versus
// skipping it only on a valid identical cached interval. Host timing is NOT FPS.
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string_view>

#include "video_core/buffer_cache/gpu_range_cache_index.h"

namespace {
constexpr size_t Slots = 512;
constexpr size_t Windows = 4096;
constexpr size_t Words = Windows / 64;
using Index = VideoCore::Detail::GpuRangeCacheIndex<Slots>;

struct Range { uint64_t address{}, size{}; };
bool Overlaps(Range a, Range b) {
    if (!a.size || !b.size) return false;
    return a.address < b.address + b.size && b.address < a.address + a.size;
}
size_t Slot(Range r) {
    return ((r.address >> 6) ^ (r.address >> 18) ^ r.size) & (Slots - 1);
}
struct State {
    std::array<std::atomic<uint64_t>, Words> bits{};
    std::array<Range, Slots> entries{};
    Index index{};
    uint64_t marks = 0;
    uint64_t hits = 0;

    void Add(Range r, bool fast) {
        const size_t slot = Slot(r);
        auto& e = entries[slot];
        // Old code marked every add; optimized code marks only misses.
        if (!fast) {
            VideoCore::Detail::MarkGpuWriteWindows(bits.data(), Windows, r.address, r.size);
            ++marks;
        }
        if (e.address == r.address && e.size == r.size) {
            ++hits;
            return;
        }
        if (fast) {
            index.Replace(slot, r.address, r.size);
            VideoCore::Detail::MarkGpuWriteWindows(bits.data(), Windows, r.address, r.size);
            ++marks;
        }
        e = r;
    }
    void Subtract(Range r, bool fast) {
        if (fast) {
            index.ForCandidates(r.address, r.size, [&](size_t slot) {
                if (Overlaps(entries[slot], r)) {
                    entries[slot] = {};
                    index.Clear(slot);
                }
            });
        } else {
            for (auto& e : entries) if (Overlaps(e, r)) e = {};
        }
        // Bitmap is MONOTONIC; subtract does not clear it.
    }
    bool Same(const State& o) const {
        for (size_t i = 0; i < Words; ++i) {
            if (bits[i].load() != o.bits[i].load()) return false;
        }
        for (size_t i = 0; i < Slots; ++i) {
            if (entries[i].address != o.entries[i].address ||
                entries[i].size != o.entries[i].size) return false;
        }
        return true;
    }
};

void Correctness() {
    State old, fast;
    std::mt19937_64 rng(0x12341183);
    std::array<Range, 128> hot{};
    for (size_t i = 0; i < 300000; ++i) {
        const size_t key = rng() % hot.size();
        Range r{};
        if (hot[key].size && (rng() % 100) < 78) {
            r = hot[key];
        } else {
            r = {(rng() % Windows) * 65536ULL, 64 + (rng() % 25000)};
            hot[key] = r;
        }
        if ((rng() % 100) < 22) {
            old.Subtract(r, false);
            fast.Subtract(r, true);
        } else {
            old.Add(r, false);
            fast.Add(r, true);
        }
        if ((i & 1023) == 0) assert(old.Same(fast));
    }
    assert(old.Same(fast));
    assert(fast.marks <= old.marks);
    assert(fast.hits == old.hits);
    assert(fast.hits > 0);
    // Huge range, wraparound and range beyond tracked address space:
    // never publish a bit for an out-of-range window.
    State boundary;
    boundary.Add({0, Windows * 65536ULL}, true);
    boundary.Add({UINT64_MAX - 5, 64}, true);
    assert(boundary.bits[Words - 1].load() != 0);
    assert(boundary.bits[0].load() != 0);
    std::printf("GPU write windows: 300000 interleaved operations equivalent; "
                "bitmap marks old=%llu indexed=%llu; cache hits=%llu\n",
                (unsigned long long)old.marks, (unsigned long long)fast.marks,
                (unsigned long long)fast.hits);
}

double Benchmark(bool fast) {
    State state;
    const Range r{0x01000000, 2 * 1024 * 1024};
    using Clock = std::chrono::steady_clock;
    auto start = Clock::now();
    for (unsigned i = 0; i < 300000; ++i) state.Add(r, fast);
    // Bind observable results to ensure the optimizer cannot discard work.
    assert(state.bits[r.address >> 22].load() != UINT64_MAX);
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

void Cost() {
    std::array<double, 7> old{}, fast{};
    for (size_t i = 0; i < old.size(); ++i) {
        if (i & 1) { fast[i] = Benchmark(true); old[i] = Benchmark(false); }
        else { old[i] = Benchmark(false); fast[i] = Benchmark(true); }
    }
    std::sort(old.begin(), old.end());
    std::sort(fast.begin(), fast.end());
    std::printf("GPU write windows (300k duplicate 2MiB writes): "
                "old=%.3fms indexed=%.3fms ratio=%.3f (host, not FPS)\n",
                old[old.size()/2], fast[fast.size()/2],
                fast[fast.size()/2]/old[old.size()/2]);
}
}

int main(int argc, char** argv) {
    Correctness();
    if (argc == 1) { Cost(); return 0; }
    return argc == 2 && std::string_view(argv[1]) == "--correctness-only" ? 0 : 2;
}
