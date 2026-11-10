// SPDX-License-Identifier: GPL-2.0-or-later
// Exact production Stage-B fallback lookup vs the old repeated stage scan.
// When there are no ring bindings for the selected shader, the new code
// avoids re-scanning all shader stages for every descriptor.
#include "video_core/renderer_vulkan/tlou_ring_stage_lookup.h"
#include <array>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>

struct Ring {unsigned index, data;};
struct Stage {const void* info; const Ring* bindings;unsigned count;};
template<std::size_t N>const Ring* Previous(
    const std::array<Stage,N>& stages,unsigned n,const void* info,unsigned idx) {
    for(unsigned i=0;i<n;++i)if(stages[i].info==info) {
        for(unsigned j=0;j<stages[i].count;++j)
            if(stages[i].bindings[j].index==idx)return &stages[i].bindings[j];
        return nullptr;
    }
    return nullptr;
}
template<std::size_t N>const Stage* SelectStage(
    const std::array<Stage,N>& stages,unsigned n,const void* info) {
    for(unsigned i=0;i<n;++i)if(stages[i].info==info)return &stages[i];
    return nullptr;
}
static volatile std::uint64_t observed=0;
int main() {
    std::array<Ring,12> rings{};
    for(unsigned i=0;i<rings.size();++i)rings[i]={i,0x1357+i};
    std::array<unsigned,8> shader_info{};
    std::array<Stage,6> stages{{
        {&shader_info[1],rings.data(),3},
        {&shader_info[3],rings.data()+3,4},
        {&shader_info[5],rings.data()+7,0},
        {&shader_info[7],rings.data()+7,5},
        {&shader_info[1],rings.data(),2}, // duplicate stage: first-match semantics
        {&shader_info[0],rings.data(),0}
    }};
    std::mt19937 gen{0x20261110};
    for(unsigned i=0;i<800000;++i) {
        const unsigned n=gen()%7;
        const unsigned target=gen()%8;
        const unsigned slot=gen()%40;
        const void* shader=&shader_info[target];
        const auto* old=Previous(stages,n,shader,slot);
        const auto* st=SelectStage(stages,n,shader);
        const auto* now=Vulkan::Detail::FindPacketRingBinding(st,slot);
        assert(old==now);
    }
    // Vary shader and ring-stage availability between submissions, while
    // keeping 32 consecutive descriptors for each submission as in the game.
    std::array<unsigned,8192> lookup_shaders{};
    for(auto& id:lookup_shaders)id=gen()%8;
    constexpr unsigned Packets=60000;
    constexpr unsigned N=Packets*32;
    auto timing=[&](bool fast){
        const auto start=std::chrono::steady_clock::now();
        std::uint64_t sum=0;
        for(unsigned packet=0;packet<Packets;++packet) {
            const void* shader=&shader_info[lookup_shaders[packet%lookup_shaders.size()]];
            const auto* selected=fast?SelectStage(stages,6,shader):nullptr;
            for(unsigned slot=0;slot<32;++slot) {
                const auto* entry=fast
                    ? Vulkan::Detail::FindPacketRingBinding(selected,slot)
                    : Previous(stages,6,shader,slot);
                if(entry)sum+=entry->data;
            }
        }
        observed=observed+sum;
        return std::chrono::duration<double,std::nano>(
            std::chrono::steady_clock::now()-start).count()/N;
    };
    std::array<double,5> old{},now{};
    for(unsigned i=0;i<5;++i) {
        if(i&1){now[i]=timing(true);old[i]=timing(false);}
        else {old[i]=timing(false);now[i]=timing(true);}
    }
    std::sort(old.begin(),old.end());std::sort(now.begin(),now.end());
    std::printf("TLOU ring stage fallback: 800k randomized exact lookups PASS; "
                "mixed-stage old=%.2fns fast=%.2fns ratio=%.3f\n",
                old[2],now[2],now[2]/old[2]);
    assert(now[2]<old[2]);
}
