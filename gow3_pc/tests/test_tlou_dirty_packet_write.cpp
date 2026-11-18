// SPDX-License-Identifier: GPL-2.0-or-later
// Exact packet ABI parity + host CPU cost for the Stage-A reg-delta encoder.
// Never interprets a host microbenchmark as actual gameplay FPS.
#include "video_core/renderer_vulkan/tlou_dirty_packet_write.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <random>
#include <vector>
using u8=std::uint8_t;using u16=std::uint16_t;using u32=std::uint32_t;using u64=std::uint64_t;

struct Dirty {
    static constexpr u32 BlockWords=32,NumBlocks=1664,NumWords=(NumBlocks+63)/64;
    std::array<u64,NumWords> blocks{};
    u64 words_set=0;
    void Set(u32 block) {
        blocks[block/64]|=u64(1)<<(block%64);
        words_set|=u64(1)<<(block/64);
    }
    template<class F>void ForEach(F f)const {
        for(u64 w=words_set;w;w&=w-1) {
            const u32 wi=std::countr_zero(w);
            for(u64 b=blocks[wi];b;b&=b-1)
                f(wi*64+std::countr_zero(b));
        }
    }
};
static u32 Align(u32 bytes){return (bytes+7u)&~u32(7);}
static u32 Legacy(const Dirty& dirty,const u32* regs,u8* out) {
    std::array<u16,Dirty::NumBlocks> blocks{};
    u32 n=0;
    dirty.ForEach([&](u32 b){blocks[n++]=static_cast<u16>(b);});
    std::memcpy(out,blocks.data(),n*sizeof(u16));
    u8* values=out+Align(n*sizeof(u16));
    for(u32 i=0;i<n;++i)
        std::memcpy(values+size_t(i)*128,regs+size_t(blocks[i])*32,128);
    return n;
}
static u32 Direct(const Dirty& dirty,const u32* regs,u8* out) {
    const u32 n=Vulkan::Detail::CountDirtyBlocks(dirty);
    Vulkan::Detail::WriteDirtyBlocks(dirty,regs,out,n);
    return n;
}
static void AssertSame(const Dirty& dirty,const u32* regs,u8* old,u8* fast) {
    const u32 n=Legacy(dirty,regs,old);
    const u32 m=Direct(dirty,regs,fast);
    assert(n==m);
    // Alignment padding has unspecified bytes in the old encoder and is ignored by Stage B.
    assert(!std::memcmp(old,fast,n*sizeof(u16)));
    assert(!std::memcmp(old+Align(n*sizeof(u16)),fast+Align(n*sizeof(u16)),size_t(n)*128));
}
static volatile u64 sink=0;
static double Bench(const std::array<Dirty,128>& cases,const u32* regs,u8* out,bool direct) {
    const auto begin=std::chrono::steady_clock::now();
    u64 checksum=0;
    constexpr unsigned Iterations=1000;
    for(unsigned k=0;k<Iterations;++k)
        for(unsigned j=0;j<cases.size();++j) {
            const auto& dirty=cases[(j+k)&127u];
            checksum+=(direct?Direct(dirty,regs,out):Legacy(dirty,regs,out));
            checksum+=out[0];
        }
    sink=sink+checksum;
    return std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-begin).count()/
           (Iterations*cases.size());
}
int main() {
    std::mt19937 gen(0x110906);
    std::vector<u32> regs(Dirty::NumBlocks*32);
    for(auto& r:regs)r=gen();
    std::vector<u8> old(Dirty::NumBlocks*130+16,0xA5),fast(old.size(),0xA5);
    std::array<u32,16> counts{0,1,2,3,4,5,8,16,24,31,32,63,64,65,256,1664};
    std::vector<u32> shuffled(Dirty::NumBlocks);
    std::iota(shuffled.begin(),shuffled.end(),0u);
    for(u32 count:counts)
        for(unsigned repeat=0;repeat<100;++repeat) {
            std::shuffle(shuffled.begin(),shuffled.end(),gen);
            Dirty dirty{};
            for(u32 i=0;i<count;++i)dirty.Set(shuffled[i]);
            AssertSame(dirty,regs.data(),old.data(),fast.data());
        }
    std::puts("Stage-A packet parity: 1600 sparse/dense permutations, IDs and values exact");
    for(u32 count:{1u,2u,4u,8u,16u}) {
        std::array<Dirty,128> cases{};
        for(auto& dirty:cases) {
            std::shuffle(shuffled.begin(),shuffled.end(),gen);
            for(u32 i=0;i<count;++i)dirty.Set(shuffled[i]);
        }
        std::array<double,5> oldNs{},directNs{};
        for(unsigned rep=0;rep<5;++rep) {
            if(rep&1u) {
                directNs[rep]=Bench(cases,regs.data(),fast.data(),true);
                oldNs[rep]=Bench(cases,regs.data(),old.data(),false);
            }else{
                oldNs[rep]=Bench(cases,regs.data(),old.data(),false);
                directNs[rep]=Bench(cases,regs.data(),fast.data(),true);
            }
        }
        std::sort(oldNs.begin(),oldNs.end());std::sort(directNs.begin(),directNs.end());
        const double ratio=directNs[2]/oldNs[2];
        std::printf("Stage-A packet %u blocks: old=%.2fns direct=%.2fns ratio=%.3f (host, not FPS)\n",
                    count,oldNs[2],directNs[2],ratio);
        if((count==2||count==4)&&ratio>=1.00) {
            std::fprintf(stderr,"Stage-A direct packet failed CPU gate at %u blocks\n",count);
            return 2;
        }
    }
    return 0;
}
