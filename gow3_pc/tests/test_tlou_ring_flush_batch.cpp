// SPDX-License-Identifier: GPL-2.0-or-later
#include "video_core/renderer_vulkan/tlou_constant_ring_flush.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>
using u64=std::uint64_t;
struct Range {u64 offset,size;};
int main() {
    constexpr u64 cap=1<<20;
    std::mt19937_64 rng{0x991bac3828ULL};
    u64 total_writes=0,total_batches=0,total_flushes=0;
    for(int t=0;t<150000;++t) {
        u64 pos=rng()%(cap*10),begin=pos;
        std::vector<Range> written,flushed;
        const int n=int(rng()%15);
        for(int k=0;k<n;++k) {
            pos+=rng()%512;
            if(pos%cap+8192>cap)pos+=cap-pos%cap;
            const u64 length=1+rng()%8192;
            written.push_back({pos%cap,length});
            pos+=length;
        }
        Vulkan::Detail::ForRingFlushSegments(begin,pos,cap,[&](u64 off,u64 size) {
            assert(size && off<cap && off+size<=cap);
            flushed.push_back({off,size});
        });
        assert(flushed.size()<=2);
        assert((pos==begin)==flushed.empty());
        for(const auto& w:written) {
            assert(std::any_of(flushed.begin(),flushed.end(),[&](const Range& f) {
                return w.offset>=f.offset && w.offset+w.size<=f.offset+f.size;
            }));
        }
        total_writes+=written.size();total_flushes+=flushed.size();++total_batches;
    }
    for(u64 begin:std::array<u64,4>{0,cap-1,cap,cap*2-63}) {
        for(u64 length:std::array<u64,7>{0,1,63,cap-1,cap,cap+1,cap*3}) {
            u64 covers=0,segments=0;
            Vulkan::Detail::ForRingFlushSegments(begin,begin+length,cap,[&](u64 off,u64 size) {
                assert(off+size<=cap);covers+=size;++segments;
            });
            assert(covers==std::min(length,cap));
            assert(segments<3);
        }
    }
    std::printf("ring flush: 150000 randomized packets and wrap boundaries passed; legacy=%llu new=%llu flush segments for %llu packets (noncoherent model, not FPS)\n",
        (unsigned long long)total_writes,(unsigned long long)total_flushes,
        (unsigned long long)total_batches);
}
