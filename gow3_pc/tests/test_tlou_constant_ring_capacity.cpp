// SPDX-License-Identifier: GPL-2.0-or-later
// TLOU Android constant-ring tier, wrap, and GPU-fence ownership tests.
#include "video_core/renderer_vulkan/tlou_constant_ring_capacity.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <random>
using u64 = std::uint64_t;
using namespace Vulkan::Detail;
constexpr u64 MiB=u64{1}<<20;
constexpr u64 GiB=u64{1}<<30;

struct Ring {
    const u64 capacity;
    u64 position=0,retired=0,success=0,failed=0;
    std::deque<u64> snapshots;
    explicit Ring(u64 cap):capacity(cap){}
    bool Allocate(u64 size, u64 alignment) {
        const u64 at=ConstantRingAlignAndWrap(position,size,alignment,capacity);
        assert(at%capacity+size<=capacity);
        if(ConstantRingOverlapsInFlight(at,size,retired,capacity)) {
            ++failed; return false;
        }
        // No guest buffer snapshot can overlap an unretired byte.
        assert(at+size>=retired && at+size-retired<=capacity);
        position=at+size;
        snapshots.push_back(position);
        ++success;return true;
    }
    void CompleteSome(unsigned count) {
        for(unsigned i=0;i<count && !snapshots.empty();++i) {
            retired=snapshots.front(); snapshots.pop_front();
        }
        assert(position>=retired&&position-retired<=capacity);
    }
};

int main() {
    static_assert(ConstantRingMiB(0)==32);
    static_assert(ConstantRingMiB(5*GiB)==32);
    static_assert(ConstantRingMiB(6*GiB)==64);
    static_assert(ConstantRingMiB(9*GiB)==64);
    static_assert(ConstantRingMiB(10*GiB)==128);
    static_assert(ConstantRingMiB(12*GiB)==128);
    static_assert(ConstantRingMiB(12*GiB,32)==32);
    static_assert(ConstantRingMiB(12*GiB,64)==64);
    static_assert(ConstantRingMiB(2*GiB,128)==128);
    static_assert(ConstantRingMiB(12*GiB,55)==128);

    for(u64 cap:std::array<u64,3>{32*MiB,64*MiB,128*MiB}) {
        Ring ring(cap);
        // Saturated 1 KiB snapshots with no GPU completion: larger
        // capacities must admit proportionally more independent packets.
        for(u64 i=0;i<180000;++i)ring.Allocate(1024,256);
        const u64 expected=cap/1024;
        assert(ring.success==expected);
        assert(ring.failed==180000-expected);
        assert(ring.position==cap);
        assert(!ring.Allocate(16,16)); // ring still in use: never overwrite
        ring.CompleteSome(expected/2);
        for(u64 i=0;i<expected/2;++i) assert(ring.Allocate(1024,256));
        assert(!ring.Allocate(1024,256)); // no unretired overlap
        // Once all past GPU work completes, the same physical pages
        // are reusable without losing order.
        ring.CompleteSome(200000);
        assert(ring.Allocate(1024,256));
        std::printf("capacity=%lluMiB initially captured=%llu/180000; safe recycle PASS\n",
                    (unsigned long long)(cap/MiB),
                    (unsigned long long)expected);
    }
    // The exact production alignment/wrap behavior with variable sizes
    // and device alignments must agree with the original implementation.
    std::mt19937_64 rng{0xA5222026};
    for(u64 cap:std::array<u64,3>{32*MiB,64*MiB,128*MiB}) {
        Ring ring(cap);
        for(u64 i=0;i<260000;++i){
            const u64 size=1+(rng()%16384);
            const u64 align=u64{1}<<(4+(rng()%9));
            const u64 old_at=[&]{
                u64 at=(ring.position+align-1)&~(align-1);
                if(at%cap+size>cap)at+=cap-at%cap;
                return at;
            }();
            const u64 at=ConstantRingAlignAndWrap(ring.position,size,align,cap);
            assert(at==old_at);
            const bool used=at+size>cap&&at+size-cap>ring.retired;
            assert(used==ConstantRingOverlapsInFlight(at,size,ring.retired,cap));
            ring.Allocate(size,align);
            if(i%150==0)ring.CompleteSome(120);
        }
        ring.CompleteSome(500000);
        assert(ring.Allocate(16384,4096));
    }
    std::puts("constant ring: 32/64/128 MiB tiers, identical wrap, 260k mixed allocations per tier PASS");
}
