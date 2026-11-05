// SPDX-License-Identifier: GPL-2.0-or-later
// Uses the production exact-key memo AND production StageSpecialization
// constructor. No APK or guessed TLOU FPS.
#include "../gpu/shadps4/video_core/renderer_vulkan/tlou_compute_perm_memo.h"
#include "../gpu/shadps4/shader_recompiler/specialization.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>
#include <cstdint>

namespace Shader::Gcn {
std::optional<FetchShaderData> ParseFetchShader(const Shader::Info&) {
    return std::nullopt; // compute shaders have no fetch shader
}
}
using u32=std::uint32_t;
using u64=std::uint64_t;
using Clock=std::chrono::steady_clock;
using Memo=Vulkan::Detail::ExactComputePermutationMemo<Shader::RuntimeInfo>;
volatile u64 escaped = 0;
static constexpr unsigned N=220'000;

static Shader::Info MakeInfo() {
    Shader::Info info{};
    info.sw_stage=Shader::SwStage::Compute;
    info.hw_stage=Shader::HwStage::Compute;
    info.has_fetch_shader=false;
    info.pgm_hash=0x22e6e19e;
    info.flattened_ud_buf.resize(64);
    for(u32 i=0;i<8;++i) {
        Shader::BufferResource b{};
        b.buffer_type=Shader::BufferType::Guest;
        b.sharp_fetch.load_mask=0xF;
        b.sharp_fetch.offsets.fill(Shader::UNKNOWN_LOCATION);
        for(u32 j=0;j<4;++j)
            b.sharp_fetch.offsets[j]=static_cast<Shader::SharpLocation>(i*4+j);
        info.buffers.push_back(b);
    }
    for(u32 i=0;i<32;++i) info.flattened_ud_buf[i]=0;
    return info;
}

static void Verify() {
    Memo cache{};
    Shader::RuntimeInfo info{};
    info.Initialize(Shader::HwStage::Compute,Shader::SwStage::Compute);
    std::array<u32,16> flat{};
    flat[3]=13;
    assert(!cache.Matches(1,flat,info,2));
    cache.Remember(1,flat,info,1);
    assert(cache.Matches(1,flat,info,2));
    assert(!cache.Matches(2,flat,info,2));
    assert(!cache.Matches(1,flat,info,1));
    flat[3] = 7; // same user-data address with changed guest descriptor contents
    assert(!cache.Matches(1,flat,info,2));
    flat[3] = 13;
    info.hw.cs.workgroup_size[0] = 32;
    assert(!cache.Matches(1,flat,info,2));
    info.hw.cs.workgroup_size[0] = 0;
    assert(cache.Matches(1,flat,info,2));
    cache.Invalidate();
    assert(!cache.Matches(1,flat,info,2));
    std::vector<u32> oversized(257,42);
    cache.Remember(1,oversized,info,1);
    assert(!cache.Matches(1,oversized,info,2));
    for(unsigned i=0;i<20000;++i) {
        auto same=flat;
        cache.Remember(123,same,info,1);
        same[i % same.size()] ^= i+1;
        assert(!cache.Matches(123,same,info,2));
    }
}

static double Bench(bool memo, const Shader::Info& in, const Shader::RuntimeInfo& runtime) {
    Memo cache{};
    Shader::Profile profile{};
    Shader::Backend::Bindings bindings{};
    const auto flat=in.FlatUserData();
    cache.Remember(in.ProgramBase(),flat,runtime,0);
    const auto t0=Clock::now();
    u64 sum=0;
    for(unsigned i=0;i<N;++i) {
        std::atomic_signal_fence(std::memory_order_seq_cst);
        if(memo && cache.Matches(in.ProgramBase(),flat,runtime,1)) {
            sum+=cache.Permutation()+1;
        } else {
            auto spec=Shader::StageSpecialization(in,runtime,profile,bindings);
            sum+=spec.buffers.size()+spec.bitset.count();
        }
        std::atomic_signal_fence(std::memory_order_seq_cst);
    }
    escaped=sum;
    return std::chrono::duration<double,std::nano>(Clock::now()-t0).count()/N;
}
int main(int argc,char** argv) {
    Verify();
    auto info=MakeInfo();
    Shader::RuntimeInfo runtime{};
    runtime.Initialize(Shader::HwStage::Compute,Shader::SwStage::Compute);
    Shader::Profile profile{};
    Shader::Backend::Bindings binding{};
    auto actual=Shader::StageSpecialization(info,runtime,profile,binding);
    assert(actual.buffers.size()==8);
    if(argc>1 && std::strcmp(argv[1],"--correctness-only")==0){
        puts("Compute permutation exact-key invalidation and production specialization: PASS");
        return 0;
    }
    for(int i=0;i<3;++i){Bench(false,info,runtime);Bench(true,info,runtime);}
    std::array<double,9> base{},cached{};
    for(std::size_t i=0;i<base.size();++i) {
        if(i%2==0){base[i]=Bench(false,info,runtime);cached[i]=Bench(true,info,runtime);}
        else{cached[i]=Bench(true,info,runtime);base[i]=Bench(false,info,runtime);}
    }
    std::sort(base.begin(),base.end());
    std::sort(cached.begin(),cached.end());
    printf("Production StageSpecialization: %.3f ns/dispatch, exact memo hit: %.3f ns/dispatch\\n",
           base[4],cached[4]);
    printf("CPU saving: %.3f ns per repeated compatible compute dispatch\\n",base[4]-cached[4]);
    printf("EXAMPLE ONLY (no observed hit rate): 1580 dispatches/frame, if all reusable: %.4f ms/frame\\n",
           1580*(base[4]-cached[4])/1e6);
    puts("Re-running the SRT walker, resource binding and synchronization is mandatory in-game.");
}
