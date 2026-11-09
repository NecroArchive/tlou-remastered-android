// SPDX-License-Identifier: GPL-2.0-or-later
// Verify the exact bit pattern of the original Stage-A pending-write filter.
// Host timings are diagnostic only: they do not imply Android FPS changes.
#include "video_core/renderer_vulkan/tlou_pending_granule_filter.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdlib>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string_view>
#include <vector>

namespace {
using u64 = std::uint64_t;
using Filter = std::array<u64, 64>;

void LegacyMark(Filter& filter, u64 begin, u64 end) {
    const u64 first = begin >> 16, last = (end - 1) >> 16;
    if (last - first >= 4096) { filter.fill(~u64{0}); return; }
    for (u64 g = first; g <= last; ++g) {
        const unsigned bit = static_cast<unsigned>(g % 4096);
        filter[bit / 64] |= u64{1} << (bit % 64);
    }
}

void ProductionMark(Filter& filter, u64 begin, u64 end) {
    const u64 first = begin >> 16, last = (end - 1) >> 16;
    if (last - first < 8) {
        for (u64 granule = first; granule <= last; ++granule) {
            const unsigned bit = static_cast<unsigned>(granule % 4096);
            filter[bit / 64] |= u64{1} << (bit % 64);
        }
        return;
    }
    Vulkan::Detail::MarkPendingGranules<4096, 16>(filter, begin, end);
}

bool LegacyHit(const Filter& filter, u64 begin, u64 end) {
    const u64 first = begin >> 16, last = (end - 1) >> 16;
    if (last - first >= 4096) return true;
    for (u64 g = first; g <= last; ++g) {
        const unsigned bit = static_cast<unsigned>(g % 4096);
        if ((filter[bit / 64] >> (bit % 64)) & 1) return true;
    }
    return false;
}

void Verify() {
    std::mt19937_64 rng{0xbadcafe};
    Filter legacy{}, optimized{};
    for (unsigned i = 0; i < 250000; ++i) {
        if ((i % 127) == 0) {
            legacy.fill(0);
            optimized.fill(0);
        }
        const u64 address = (rng() % 262144ull) * 65536 + (rng() % 65536);
        const u64 size = i % 137 == 0 ? u64{4097} * 65536 :
                         i % 7 == 0 ? u64{300} * 65536 :
                         i % 11 == 0 ? u64{5} * 65536 :
                                       1 + rng() % 65536;
        LegacyMark(legacy, address, address + size);
        ProductionMark(optimized, address, address + size);
        if ((i & 127) == 0) {
            assert(legacy == optimized);
            for (unsigned j = 0; j < 16; ++j) {
                const u64 at = (rng() % 262144ull) * 65536 + rng() % 65536;
                const u64 extent = j % 5 == 0 ? u64{4097} * 65536 :
                                   j % 3 == 0 ? u64{255} * 65536 :
                                                1 + rng() % 524288;
                assert(LegacyHit(legacy, at, at + extent) ==
                       LegacyHit(optimized, at, at + extent));
            }
        }
    }
    assert(legacy == optimized);
    puts("TLOU pending-granule equivalence: 250000 randomized writes and overlapping reads passed");
}

static volatile u64 observable;
double Benchmark(bool optimized, u64 size) {
    std::mt19937_64 rng{11};
    std::vector<u64> addresses;
    addresses.reserve(300000);
    for (unsigned i = 0; i < 300000; ++i) {
        addresses.push_back((rng() % 262144ull) * 65536 + rng() % 65536);
    }
    Filter filter{};
    const auto start = std::chrono::steady_clock::now();
    for (u64 address : addresses) {
        if (optimized) {
            ProductionMark(filter, address, address + size);
        } else {
            LegacyMark(filter, address, address + size);
        }
    }
    observable = observable + filter[0];
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
}

void Cost(bool gate) {
    for (const u64 size : {u64{1024}, u64{2} << 20, u64{16} << 20}) {
        std::array<double, 7> legacy{}, optimized{};
        for (unsigned i = 0; i < legacy.size(); ++i) {
            if (i & 1) {
                optimized[i] = Benchmark(true, size);
                legacy[i] = Benchmark(false, size);
            } else {
                legacy[i] = Benchmark(false, size);
                optimized[i] = Benchmark(true, size);
            }
        }
        std::sort(legacy.begin(), legacy.end());
        std::sort(optimized.begin(), optimized.end());
        const double ratio = optimized[3] / legacy[3];
        std::printf("pending-granule mark size=%llu: old=%.3fms selected=%.3fms ratio=%.3f (host, not FPS)\n",
                    (unsigned long long)size, legacy[3], optimized[3], ratio);
        // A/B CPU gate: large pending write marks must beat the old path.
        // The 1 KiB path is deliberately kept identical to the legacy loop.
        if (gate && size == (u64{2} << 20) && ratio >= 0.80) {
            std::fprintf(stderr, "ERROR: 2 MiB mark speedup not measured\n");
            std::abort();
        }
        if (gate && size == (u64{16} << 20) && ratio >= 0.55) {
            std::fprintf(stderr, "ERROR: 16 MiB mark speedup not measured\n");
            std::abort();
        }
    }
}
} // namespace

int main(int argc, char** argv) {
    Verify();
    if (argc == 1) { Cost(false); return 0; }
    if (argc == 2 && std::string_view(argv[1]) == "--cost-gate") {
        Cost(true);
        return 0;
    }
    return argc == 2 && std::string_view(argv[1]) == "--correctness-only" ? 0 : 2;
}
