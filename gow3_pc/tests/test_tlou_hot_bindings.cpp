// SPDX-License-Identifier: GPL-2.0-or-later
// Exercise the REAL Shader::BufferResource overload, including postops and
// invalid V# descriptors, not a synthetic substitute for shader semantics.
// This is a host microbench; only on-device gameplay measures FPS.
#include "shader_recompiler/resource.h"
#include "video_core/renderer_vulkan/tlou_readback_hot_stats.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <span>

struct TestInfo {
    std::array<u32,128> flat{};
    u64 pgm_hash=0; // never effect shader, so no guest mapping probes in host test
    u64 pgm_base=0;
    std::span<const u32> FlatUserData() const noexcept { return flat; }
    u64 ProgramBase() const noexcept { return pgm_base; }
};
static volatile u64 sink=0;

template<bool Hoist>
double Benchmark(const std::array<Shader::BufferResource,8>& resources,
                 const TestInfo& info) {
    auto start=std::chrono::steady_clock::now();
    u64 checksum=0;
    for(unsigned iteration=0;iteration<200000;++iteration) {
        const u32* flat=Hoist ? info.FlatUserData().data() : nullptr;
        for(const auto& desc:resources) {
            const auto sharp=Hoist ? desc.GetSharp(info,flat) : desc.GetSharp(info);
            checksum+=sharp.base_address;
        }
    }
    sink=sink+checksum;
    return std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/
           (200000*resources.size());
}

int main() {
    using Shader::BufferResource;
    TestInfo info{};
    std::mt19937 rng(0x20261110);
    for(auto& value:info.flat)value=rng();
    std::array<BufferResource,8> desc{};
    for(unsigned i=0;i<desc.size();++i) {
        desc[i].buffer_type=Shader::BufferType::Guest;
        desc[i].sharp_fetch.load_mask=15;
        for(unsigned j=0;j<4;++j) {
            desc[i].sharp_fetch.offsets[j]=i*4+j;
            desc[i].sharp_fetch.immediates[j]=rng();
        }
    }
    for(unsigned trial=0;trial<200000;++trial) {
        info.pgm_base=trial*16;
        auto& d=desc[trial%desc.size()];
        d.post_op=static_cast<Shader::SharpFetchPostOp>(trial%3);
        d.post_op_dw1_mask=rng();
        d.sharp_fetch.load_mask=trial%2 ? 15 : 10;
        if(trial%5==0) d.sharp_fetch.offsets[0]=Shader::UNKNOWN_LOCATION;
        else d.sharp_fetch.offsets[0]=(trial%3)*4;
        if(d.sharp_fetch.offsets[0]!=Shader::UNKNOWN_LOCATION) {
            for(unsigned j=1;j<4;++j)
                d.sharp_fetch.offsets[j]=d.sharp_fetch.offsets[0]+j;
        }
        const auto old=d.GetSharp(info);
        const auto fast=d.GetSharp(info,info.FlatUserData().data());
        assert(std::memcmp(&old,&fast,sizeof(old))==0);
    }
    // Return to the common 4-word direct guest path for timing.
    for(unsigned i=0;i<desc.size();++i) {
        auto& d=desc[i];
        d.sharp_fetch.load_mask=15;
        d.post_op=Shader::SharpFetchPostOp::None;
        for(unsigned j=0;j<4;++j)d.sharp_fetch.offsets[j]=i*4+j;
    }
    std::array<double,5> baseline{},hoist{};
    for(unsigned k=0;k<5;++k) {
        if(k%2) {
            hoist[k]=Benchmark<true>(desc,info);
            baseline[k]=Benchmark<false>(desc,info);
        }else{
            baseline[k]=Benchmark<false>(desc,info);
            hoist[k]=Benchmark<true>(desc,info);
        }
    }
    std::sort(baseline.begin(),baseline.end());std::sort(hoist.begin(),hoist.end());
    std::printf("TLOU exact-sharp: 200000 per-descriptor variants byte-identical; "
                "batch8 old=%.2fns new=%.2fns ratio=%.3f (host, not FPS)\n",
                baseline[2],hoist[2],hoist[2]/baseline[2]);

    Vulkan::Detail::TlouReadbackHotStats stats;
    u64 expected=0,reports=0;
    for(u64 i=0;i<5000000;++i) {
        const bool eligible=(i%13)==0;
        expected+=eligible;
        if(stats.Note(eligible)) ++reports;
    }
    assert(stats.TakeAccepted()==expected);
    assert(stats.TakeAccepted()==0);
    assert(reports==5000000/512);
    std::puts("TLOU readback hot telemetry: 5M decisions match old atomic count, mode unchanged PASS");
}
