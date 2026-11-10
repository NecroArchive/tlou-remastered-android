// SPDX-License-Identifier: GPL-2.0-or-later
// Graphics pipeline cached-hit service gate, exact cold/async/miss semantics.
// No guest register, snapshot, or graphics pipeline key is memoized.
#include "video_core/renderer_vulkan/tlou_compute_cached_hit_gate.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <random>
#include <unordered_map>

using Vulkan::Detail::ComputeCachedHitServiceGate;

struct Full {
    std::unordered_map<std::uint32_t, std::uint32_t> compiled;
    std::uint32_t ready = 0;
    std::uint64_t serviced = 0, draws = 0, key_constructed = 0;
    std::uint32_t Get(std::uint32_t key) {
        ++serviced;
        if (ready) { compiled[ready] = ready; ready = 0; }
        ++key_constructed;
        ++draws;
        if (!compiled.contains(key)) compiled[key] = key;
        return compiled[key];
    }
};
struct Optimized {
    std::unordered_map<std::uint32_t, std::uint32_t> compiled;
    ComputeCachedHitServiceGate gate;
    std::uint32_t ready = 0;
    std::uint64_t serviced = 0, draws = 0, key_constructed = 0, fast = 0, misses = 0;
    std::uint32_t Get(std::uint32_t key) {
        ++draws;
        if (gate.TryCachedHit(ready != 0)) {
            // Production still calls RefreshGraphicsKey on every lookup.
            ++key_constructed;
            if (auto it = compiled.find(key); it != compiled.end()) {
                ++fast;
                return it->second;
            }
            ++misses;
            gate.OnCacheMiss();
        }
        ++serviced;
        if (ready) { compiled[ready] = ready; ready = 0; }
        ++key_constructed;
        if (!compiled.contains(key)) compiled[key] = key;
        return compiled[key];
    }
};
int main() {
    ComputeCachedHitServiceGate gate;
    std::uint64_t polls = 0, fast = 0;
    for (unsigned i = 0; i < 1000000; ++i) {
        if (gate.TryCachedHit(false)) ++fast;
        else ++polls;
    }
    assert(polls == 15625 && fast == 984375);
    assert(!gate.TryCachedHit(true));
    assert(gate.TryCachedHit(false));

    Full original;
    Optimized new_path;
    std::mt19937 gen{0x20261110};
    std::uint32_t previous = 3;
    for (unsigned i = 0; i < 1000000; ++i) {
        // Guest state changes each draw even when the compiled pipeline key
        // repeats. Ready shader/graphics builds must service immediately.
        if (i % 173 == 0) {
            original.ready = 1 + gen() % 256;
            new_path.ready = original.ready;
        }
        std::uint32_t key = gen() % 100 < 96 ? previous : 1 + gen() % 420;
        previous = key;
        assert(original.Get(key) == new_path.Get(key));
        assert(original.ready == new_path.ready);
        if ((i & 2047u) == 0u) {
            assert(original.compiled == new_path.compiled);
        }
    }
    assert(original.compiled == new_path.compiled);
    assert(original.draws == new_path.draws && original.draws == 1000000);
    // A fast probe followed by an absent pipeline must reconstruct its key
    // after servicing compiler completions, exactly as the production fallback.
    // No draw skips key construction; only misses deliberately do it twice.
    assert(new_path.key_constructed == original.key_constructed + new_path.misses);
    assert(new_path.serviced < original.serviced / 6);
    assert(new_path.fast > 800000 && new_path.misses > 0);
    std::printf("graphics cached-hit: 1M draws, pipeline-key parity PASS; "
                "fast=%llu full-service=%llu misses=%llu, periodic polls=%llu\n",
                (unsigned long long)new_path.fast,
                (unsigned long long)new_path.serviced,
                (unsigned long long)new_path.misses,
                (unsigned long long)polls);
}
