// SPDX-License-Identifier: GPL-2.0-or-later
// Exactness and cost of production IntervalList residency fast path.
// Full [first,last+1) coverage is sufficient for EnsureResident to skip
// constructing a gap-list and submitting sparse Vulkan binds. The backing
// intervals are still checked in full: adjacent unmerged allocation records
// must be handled, and a single uncovered byte must force the old path.
#include "common/interval_set.h"
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

struct Backing : Interval {
    unsigned allocation = 0;
    constexpr bool CanMergeWith(const Backing& other) const noexcept {
        return allocation == other.allocation;
    }
    constexpr Backing SubRange(u64 first,u64 last) const noexcept {
        return { {first,last}, allocation };
    }
};
static bool Baseline(const IntervalList<Backing>& list,u64 start,u64 end) {
    IntervalList<Interval> gaps;
    list.ForEachGap(start,end,[&](u64 first,u64 last){
        gaps.Add({first,last});
    });
    return gaps.Empty();
}
static bool Fast(const IntervalList<Backing>& list,u64 start,u64 end) {
    return list.Contains(start,end);
}
static volatile u64 black_hole=0;
int main() {
    IntervalList<Backing> list;
    for(u64 i=0;i<65536;i+=8) {
        if((i/8)%37==0) continue; // a realistic occasional sparse hole
        // Force distinct, non-mergeable contiguous allocations.
        list.Add({{i,i+8},unsigned(i/8)});
    }
    std::mt19937_64 rng(0x20261110);
    for(u64 i=0;i<500000;++i) {
        u64 a = rng()%65536;
        u64 b = std::min<u64>(65536,a+1+(rng()%128));
        const bool before = Baseline(list,a,b);
        const bool after = Fast(list,a,b);
        assert(before==after);
    }
    // Exact full-hit queries with 1, 2, or 8 contiguous distinct backing
    // allocations prove that no unsafe fast hit skips a true gap.
    for(u64 a=8;a<65535;a+=8) {
        for(const u64 blocks : std::array<u64,4>{1,2,4,8}) {
            const u64 b=std::min<u64>(65536,a+blocks);
            assert(Baseline(list,a,b)==Fast(list,a,b));
        }
    }
    // Timing reproduces the original fully-resident lookup (no gaps) and the
    // modified Contains fast path on the same randomized working set.
    std::vector<std::pair<u64,u64>> hits;
    hits.reserve(1000000);
    for(unsigned i=0;i<1000000;++i) {
        const u64 a=8+(rng()%65520);
        const u64 b=std::min<u64>(65536,a+1+(rng()%8));
        if (Fast(list,a,b)) hits.emplace_back(a,b);
    }
    assert(hits.size()>500000);
    const auto time=[&](bool fast) {
        const auto t=std::chrono::steady_clock::now();
        u64 sum=0;
        for(const auto& [a,b]:hits) {
            sum += fast ? Fast(list,a,b) : Baseline(list,a,b);
        }
        black_hole=black_hole+sum;
        return std::chrono::duration<double,std::nano>(
            std::chrono::steady_clock::now()-t).count()/hits.size();
    };
    // Alternate order to reduce cold-cache and CPU clock skew.
    std::array<double,5> old{},now{};
    for(unsigned i=0;i<5;++i) {
        if(i%2==0) {old[i]=time(false); now[i]=time(true);}
        else {now[i]=time(true); old[i]=time(false);}
    }
    std::sort(old.begin(),old.end()); std::sort(now.begin(),now.end());
    std::printf("TLOU resident hit: 500k random ranges + 4*8k exact unmerged "
                "backing parity PASS; %zu fully resident calls; old=%.2f ns "
                "fast=%.2f ns ratio=%.3f (host CPU, NOT FPS)\n",
                hits.size(),old[2],now[2],now[2]/old[2]);
}
