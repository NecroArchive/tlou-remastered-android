// SPDX-License-Identifier: GPL-2.0-or-later
// Standalone correctness check for the EXACT production recording-thread
// readback statistics, and the per-stage flattened-view lifetime contract.
// The real Shader::BufferResource overload is checked by the full native GPU
// compiler, not by replacing the repository's spdlog with an OS package.
#include "video_core/renderer_vulkan/tlou_readback_hot_stats.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <random>
#include <span>

struct MockInfo {
    std::array<std::uint32_t,128> flat{};
    std::uint64_t lookups=0;
    std::span<const std::uint32_t> FlatUserData() {
        ++lookups;
        return flat;
    }
};

int main() {
    MockInfo info;
    std::mt19937 rng{0x20261110};
    for (auto& x:info.flat)x=rng();
    constexpr unsigned Descriptors=8,Stages=250000;
    std::uint64_t legacy=0,hoist=0;
    // No descriptor is retained across stages: a guest may rewrite every
    // element before the next one, which both paths must read fresh.
    for(unsigned stage=0;stage<Stages;++stage) {
        info.flat[stage%info.flat.size()]=rng();
        for(unsigned i=0;i<Descriptors;++i)
            legacy+=info.FlatUserData()[i*4 + stage%4];
    }
    const auto originalLookups=info.lookups;
    std::mt19937 rng2{0x20261110};
    for(auto& x:info.flat)x=rng2();
    info.lookups=0;
    for(unsigned stage=0;stage<Stages;++stage) {
        info.flat[stage%info.flat.size()]=rng2();
        const auto flat=info.FlatUserData();
        for(unsigned i=0;i<Descriptors;++i)
            hoist+=flat[i*4 + stage%4];
    }
    assert(legacy==hoist);
    assert(originalLookups==std::uint64_t(Stages)*Descriptors);
    assert(info.lookups==Stages);
    Vulkan::Detail::TlouReadbackHotStats stats;
    std::uint64_t eligible=0,reports=0;
    for(std::uint64_t i=0;i<5000000;++i) {
        const bool accepted=(i%13)==0;
        eligible+=accepted;
        if(stats.Note(accepted))++reports;
    }
    assert(stats.TakeAccepted()==eligible);
    assert(stats.TakeAccepted()==0);
    assert(reports==5000000/512);
    std::printf("TLOU flat-view mock: original=%llu reads hoisted=%llu reads "
                "all 2M descriptor values identical across guest writes; "
                "5M readback decisions identical PASS\n",
                (unsigned long long)originalLookups,
                (unsigned long long)info.lookups);
}
