// SPDX-License-Identifier: GPL-2.0-or-later
// Stage-A compute pipeline fast-hit service cadence model.
//
// Unlike unsafe draw batching, this changes only pipeline CACHE lookup
// maintenance. Every dispatch still invokes RefreshComputeKey and PostDraw
// with its own per-draw guest snapshots.
#include "video_core/renderer_vulkan/tlou_compute_cached_hit_gate.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <random>
#include <unordered_map>

using Vulkan::Detail::ComputeCachedHitServiceGate;

int main() {
    using Gate = ComputeCachedHitServiceGate;
    static_assert(Gate::ServiceInterval==64);
    Gate gate;
    std::uint64_t serviced=0, fast=0, misses=0;
    for(std::uint64_t i=0;i<1000000;++i) {
        if (gate.TryCachedHit(false)) ++fast;
        else ++serviced;
    }
    // The first lookup and each 64th are serviced; never defer forever.
    assert(serviced==15625 && fast==984375);
    assert(!gate.TryCachedHit(true));
    assert(gate.TryCachedHit(false));
    gate.OnCacheMiss();
    // The miss falls through to the full maintenance path immediately;
    // the following lookup can already use the hit path again.
    assert(gate.TryCachedHit(false));
    assert(gate.TryCachedHit(false));

    // Simulate the exact production decision: full on cold lookup, any
    // compiler/prefetch work, or at most 63 consecutive warmed cache hits.
    // Async ready work must not be dropped; calls without ready work
    // still preserve every per-dispatch key/snapshot instance.
    struct Baseline {
        std::unordered_map<std::uint32_t,std::uint32_t> compiled;
        std::uint32_t pending_id=0;
        std::uint64_t full=0, snapshot=0;
        void Service() {
            ++full;
            if(pending_id) {
                compiled[pending_id]=pending_id;
                pending_id=0;
            }
        }
        std::uint32_t Lookup(std::uint32_t key) {
            Service();
            ++snapshot;
            if (!compiled.contains(key))compiled[key]=key;
            return compiled[key];
        }
    };
    struct Fast {
        std::unordered_map<std::uint32_t,std::uint32_t> compiled;
        Gate gate;
        std::uint32_t pending_id=0;
        std::uint64_t full=0, fast=0, snapshot=0, misses=0;
        void Service() {
            ++full;
            if(pending_id) {
                compiled[pending_id]=pending_id;
                pending_id=0;
            }
        }
        std::uint32_t Lookup(std::uint32_t key) {
            // Analogous to calling RefreshComputeKey unconditionally and
            // performing Stage-A snapshot unconditionally on every draw.
            ++snapshot;
            if(gate.TryCachedHit(pending_id!=0)) {
                if(auto it=compiled.find(key); it!=compiled.end()) {
                    ++fast;return it->second;
                }
                ++misses;
                gate.OnCacheMiss();
            }
            Service();
            if (!compiled.contains(key))compiled[key]=key;
            return compiled[key];
        }
    };
    Baseline base;
    Fast cached;
    std::mt19937 gen{0x20261110};
    std::uint32_t previous=1;
    for(unsigned i=0;i<1000000;++i) {
        // Most TLOU compute dispatches use stable existing shader variants,
        // but occasionally workers produce a new pipeline, and new shader
        // variants appear in the guest register stream.
        if (i%101==0) {
            base.pending_id=100+gen()%128;
            cached.pending_id=base.pending_id;
        }
        std::uint32_t key=(gen()%100<96) ? previous : 1+(gen()%400);
        previous=key;
        assert(base.Lookup(key)==cached.Lookup(key));
        if ((i & 1023u) == 0) assert(base.compiled==cached.compiled);
    }
    assert(base.snapshot==cached.snapshot && base.snapshot==1000000);
    assert(cached.full<base.full/5); // the common-hit dispatch does less work
    assert(cached.fast>800000 && cached.misses>0);
    std::printf("compute cached-hit: 1M stable lookups -> %llu periodic polls; "
                "randomized 1M dispatches: fast=%llu full=%llu misses=%llu, "
                "all per-dispatch snapshot counts and compiled keys exact PASS\n",
                (unsigned long long)serviced,
                (unsigned long long)cached.fast,
                (unsigned long long)cached.full,
                (unsigned long long)cached.misses);
}
