// SPDX-License-Identifier: GPL-2.0-or-later
#include "../gpu/shadps4/video_core/amdgpu/tlou_shader_scan_profile.h"
#include "../gpu/shadps4/video_core/amdgpu/tlou_shader_signature_scan.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
using u32=std::uint32_t;
static constexpr u32 Marker=0x5362724fu;
int main(){
    std::array<u32,16392> code{};
    const auto* first=code.data();
    code[800]=Marker;
    const auto slow=[&]()->const u32*{
        for(auto* p=first;p<first+0x4000;++p)if(*p==Marker)return p;
        return nullptr;
    };
    const auto neon=[&]()->const u32*{
        return AmdGpu::Detail::FindShaderSignature32(
            first,first+0x4000,Marker,[&](const u32* p){return *p==Marker;});
    };
    AmdGpu::Detail::ShaderScanProfiler profiler;
    for(unsigned i=0;i<2560;++i)profiler.Observe(first,first+800,false,slow,neon);
    for(unsigned i=0;i<40;++i)profiler.Observe(first,first+800,true,slow,neon);
    const auto& stats=profiler.Snapshot();
    assert(stats.calls==2600);
    assert(stats.direct==40);
    assert(stats.fallback==2560);
    assert(stats.offsets[1]==2560); // 800 DWORDs
    assert(stats.samples==10);
    assert(stats.scalar_ns>0);
    assert(stats.neon_ns>0);
    assert(stats.mismatches==0);
    // A deliberately wrong scanner must be flagged, never accepted silently.
    AmdGpu::Detail::ShaderScanProfiler bad;
    for(unsigned i=0;i<256;++i)
        bad.Observe(first,first+800,false,slow,[&]()->const u32*{return first;});
    assert(bad.Snapshot().mismatches==1);
    std::puts("shader scan real-runtime profiler counters and A/B parity PASS");
}
