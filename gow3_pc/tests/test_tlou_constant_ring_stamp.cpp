// SPDX-License-Identifier: GPL-2.0-or-later
// Exercises the exact ConstantRing Stage-B stamp helper. In particular,
// exhausted ring positions remain unchanged, while scheduler ticks advance.
#include "video_core/renderer_vulkan/tlou_constant_ring_stamp.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <optional>
#include <random>
#include <utility>
using u64=std::uint64_t;

struct RingModel {
    static constexpr u64 Capacity = 256;
    Vulkan::Detail::ConstantRingStampProgress stamps{};
    struct Mark {u64 end,tick;};
    std::deque<Mark> ready;
    u64 position=0,retired=0,tick=0;
    std::int64_t completed=-1;
    bool legacy=false;
    u64 publications=0,failures=0,retire_calls=0;

    void Stamp() {
        if (legacy) {
            // The original early return in production ConstantRing::Stamp:
            // this cannot move forward if a saturated ring never allocates.
            if (position==stamps.pending_end) return;
            if (tick!=stamps.pending_tick &&
                stamps.pending_end!=stamps.last_published) {
                Publish(stamps.pending_end,stamps.pending_tick);
            }
            stamps.pending_end=position;
            stamps.pending_tick=tick;
            return;
        }
        stamps.Stamp(position,tick,[this](u64 end,u64 prior){Publish(end,prior);});
    }
    void Publish(u64 end,u64 when) {
        ready.push_back({end,when});
        stamps.last_published=end;
        ++publications;
    }
    void Retire() {
        ++retire_calls;
        while (!ready.empty() && static_cast<std::int64_t>(ready.front().tick)<=completed) {
            retired=ready.front().end;
            ready.pop_front();
        }
    }
    std::optional<u64> Allocate(u64 size,u64 alignment=16) {
        assert(size && size<=Capacity/2 && (alignment&(alignment-1))==0);
        u64 at=(position+alignment-1)&~(alignment-1);
        if (at%Capacity+size>Capacity) at+=Capacity-at%Capacity;
        const auto used=[&]{return at+size>Capacity && at+size-Capacity>retired;};
        if (used()) Retire();
        if (used()) {++failures;return std::nullopt;}
        position=at+size;
        return at%Capacity;
    }
};

int main() {
    // The old implementation permanently wedges the ring even after GPU
    // signals completion, because no new position => no new stamp publication.
    RingModel old{},fixed{};
    old.legacy=true;
    for(RingModel* ring:{&old,&fixed}) {
        for(int i=0;i<16;++i) {
            assert(ring->Allocate(16));
            ring->Stamp();
        }
        assert(!ring->Allocate(16));
        assert(ring->stamps.pending_end==256);
        ring->tick=1;
        ring->Stamp(); // no allocated bytes in this new tick
        ring->completed=0;
    }
    assert(!old.Allocate(16));
    assert(old.publications==0);
    assert(fixed.publications==1);
    assert(fixed.ready.front().tick==0);
    assert(fixed.Allocate(16));
    assert(fixed.retired==256);
    assert(old.retired==0);
    // A new tick alone must NEVER retire ring positions before the GPU
    // semaphore confirms the submission's completion.
    {
        RingModel ring;
        for(int i=0;i<16;++i){assert(ring.Allocate(16));ring.Stamp();}
        ring.tick=1;
        ring.Stamp();
        assert(!ring.Allocate(16)); // completed=-1
        ring.completed=0;
        assert(ring.Allocate(16));
    }
    // Publishing with an exhausted queue is allowed to fail. It must retry
    // on the next tick even if the producer end never changes.
    {
        Vulkan::Detail::ConstantRingStampProgress state;
        u64 tries=0;
        state.Stamp(64,0,[&](u64,u64){assert(false);});
        state.Stamp(64,1,[&](u64 end,u64 tick){++tries;assert(end==64&&tick==0);});
        assert(tries==1);
        state.Stamp(64,2,[&](u64 end,u64 tick){
            ++tries; assert(end==64 && tick==1);state.last_published=end;
        });
        assert(tries==2);
        state.Stamp(64,3,[&](u64,u64){assert(false);});
        assert(tries==2);
    }
    // Randomized 100k transitions, including many exhausted rings and
    // period-boundary ticks with no new allocations.
    {
        RingModel ring;
        std::mt19937 rng(0x9951ABCC);
        u64 successes=0,misses=0;
        for(unsigned step=0;step<100000;++step) {
            if(step%7==0) {++ring.tick;ring.Stamp();}
            if(step%13==0 && ring.tick>1) {
                ring.completed=static_cast<std::int64_t>(ring.tick-1);
                ring.Retire();
            }
            const u64 size=16u*((rng()%4)+1);
            auto offset=ring.Allocate(size);
            if(!offset) {
                ++misses;
                // Same position, later tick; submit the previous epoch.
                ++ring.tick;
                ring.Stamp();
                ring.completed=static_cast<std::int64_t>(ring.tick-1);
                offset=ring.Allocate(size);
            }
            assert(offset);
            assert(*offset+size<=RingModel::Capacity);
            ring.Stamp();
            ++successes;
        }
        assert(successes==100000 && misses>0 && ring.publications>0);
        std::printf("ring stamp: 100k randomized draws, %llu recoveries, %llu stamp publications PASS\n",
                    (unsigned long long)misses,(unsigned long long)ring.publications);
    }
    std::puts("ring stamp: old ring reproduces permanent exhaustion; fixed ring recovers after GPU tick PASS");
}
