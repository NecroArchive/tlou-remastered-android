// SPDX-License-Identifier: GPL-2.0-or-later
// The same recycle gate used by production ConstantRing::Allocate.
// Verify both GPU progress safety and bounded delayed fallback.
#include "video_core/renderer_vulkan/tlou_constant_ring_recycle_gate.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <random>
using u64=std::uint64_t;
using Vulkan::Detail::ConstantRingRecycleGate;

int main() {
    static_assert(ConstantRingRecycleGate::MaxBlockedBeforeProbe==32);
    ConstantRingRecycleGate gate;
    assert(gate.ShouldProbe(0,0)); // first blocked allocation checks immediately
    // No visible stamp or tick progress: at most one real Vulkan probe in 32
    // attempts, regardless of how many ring buffers failed to allocate.
    unsigned probes=1;
    for (unsigned i=1;i<100000;++i) probes+=gate.ShouldProbe(0,0);
    assert(probes>=3125 && probes<=3126);
    // A new producer stamp must bypass the throttle immediately. A completed
    // GPU tick observed by another thread must do the same.
    assert(gate.ShouldProbe(1,0));
    assert(!gate.ShouldProbe(1,0));
    assert(gate.ShouldProbe(1,1));
    assert(!gate.ShouldProbe(1,1));

    struct Model {
        ConstantRingRecycleGate gate{};
        u64 published_head=0,known_tick=0,completed_tick=0,retired=0;
        u64 position=256;
        unsigned refreshes=0,failures=0;
        bool attempt() {
            constexpr u64 Capacity=256,Size=16;
            const u64 at=position;
            if(at+Size>Capacity && at+Size-Capacity>retired) {
                if (gate.ShouldProbe(published_head,known_tick)) {
                    ++refreshes;
                    if(published_head && completed_tick>=1) {
                        known_tick=completed_tick;
                        retired=256;
                    }
                }
                if(at+Size>Capacity && at+Size-Capacity>retired) {
                    ++failures;
                    return false; // original safe Stage-B binding path
                }
            }
            position=at+Size;
            return true;
        }
    };
    // Entire ring full, no stamp published: absolutely no premature reuse.
    Model m;
    for(int i=0;i<1024;++i)assert(!m.attempt());
    assert(m.refreshes<=34 && m.retired==0);
    // GPU completion without a published stamp is insufficient.
    m.completed_tick=1;
    for(int i=0;i<128;++i)assert(!m.attempt());
    assert(m.retired==0);
    // The prior draw's final position is published. Recover immediately.
    m.published_head=1;
    assert(m.attempt());
    assert(m.retired==256);
    // Continuation: if GPU reports a completed tick to another thread,
    // immediately re-probe instead of waiting for 32 more failures.
    Model n;
    assert(!n.attempt());
    n.published_head=1;
    assert(!n.attempt());
    n.known_tick=1;
    n.completed_tick=1;
    assert(n.attempt());
    // If GPU completion occurs externally but the cached tick does not move,
    // the fallback may be used for at most 31 extra blocked allocations.
    Model p;
    p.published_head=1;
    p.completed_tick=0;
    assert(!p.attempt());
    p.completed_tick=1;
    unsigned delayed=0;
    while (!p.attempt()) {
        assert(++delayed<=ConstantRingRecycleGate::MaxBlockedBeforeProbe);
    }
    assert(delayed<=31 && p.retired==256);

    std::mt19937_64 rng(0x20261109);
    ConstantRingRecycleGate randomized;
    u64 head=0,tick=0;
    unsigned checks=0,skips=0;
    for(unsigned i=0;i<300000;++i) {
        if((rng()%1000)==0)++head;
        if((rng()%300)==0)++tick;
        const bool changed=(head!=0&&i==0) || false;
        (void)changed;
        if(randomized.ShouldProbe(head,tick))++checks;else ++skips;
    }
    assert(checks<30000 && skips>270000);
    std::printf("TLOU constant ring recycle gate: 100k consecutive misses -> %u polls; "
                "300k interleaved events -> %u polls; bounded GPU progress recovery PASS\n",
                probes,checks);
}
