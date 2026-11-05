// SPDX-License-Identifier: GPL-2.0-or-later
// The production image-base prefilter and production multi-level page table.
// Baseline deliberately UNDERCOUNTS the real invalidation path: no image
// flags, descriptor work, alias invalidation, or vector allocations.
#include "../gpu/shadps4/video_core/texture_cache/tlou_image_write_filter.h"
#include "../gpu/shadps4/video_core/multi_level_page_table.h"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <numeric>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
using u64=std::uint64_t;
using u32=std::uint32_t;
struct Traits {
    using Entry=std::vector<u32>;
    static constexpr std::size_t AddressSpaceBits=40;
    static constexpr std::size_t FirstLevelBits=10;
    static constexpr std::size_t PageBits=20;
};
struct Scene {
    VideoCore::Detail::ImageBaseWriteFilter filter;
    VideoCore::MultiLevelPageTable<Traits> table;
    std::mutex lock;
    std::vector<u64> image_bases;
    void Setup() {
        for(u64 i=1;i<=2048;++i) {
            const u64 address=(u64{0x20}<<32)+i*0x1810000ull;
            image_bases.push_back(address);
            filter.Register(address);
            table[address>>20].push_back(static_cast<u32>(i));
        }
    }
    bool OriginalMiss(u64 address,u64 size) {
        std::scoped_lock lk{lock};
        const u64 lo=address>>Traits::PageBits;
        const u64 hi=(address+size-1)>>Traits::PageBits;
        for(u64 page=lo;page<=hi;++page){
            const auto* ids=table.find(page);
            if(ids && !ids->empty()) return false;
        }
        return true;
    }
};
volatile u64 sink;
static void Verify(Scene& scene) {
    scene.Setup();
    // None of the recorded registered image bases may ever be a negative.
    for(const u64 address:scene.image_bases) {
        if(!scene.filter.MayContain(address)) std::abort();
    }
    // Simulate image GC: clearing a page table entry must NOT clear the
    // monotonically set bit. Re-registering and remapping are safe.
    scene.table[scene.image_bases[100]>>20].clear();
    if(!scene.filter.MayContain(scene.image_bases[100])) std::abort();
    scene.filter.Register(scene.image_bases[100]+1);
    if(!scene.filter.MayContain(scene.image_bases[100]+1)) std::abort();
    u64 neg=0,positives=0;
    for(u64 i=1;i<50000;++i){
        const u64 q=(u64{0x30}<<32)+i*0x8110ull;
        if(scene.filter.MayContain(q))++positives;else ++neg;
    }
    std::printf("correctness: 2048 registered bases, zero false negatives, 50000 unrelated probes: %llu false positives, %llu negatives\\n",
        (unsigned long long)positives,(unsigned long long)neg);
}
static double Run(Scene& scene,bool filter,unsigned pages){
    constexpr unsigned N=220000;
    const auto begin=Clock::now();
    u64 count=0;
    for(unsigned i=0;i<N;++i){
        // Addresses deliberately DO NOT match any registered base;
        // varied over one gigabyte just like disjoint guest buffers.
        const u64 q=(u64{0x30}<<32)+(u64(i*1349u%16384u)<<20)+0x400;
        if(filter){
            // This early miss is the actual short circuit in TextureCache;
            // if a collision occurs, it must fall back to normal lookup.
            if(!scene.filter.MayContain(q))++count;
            else count+=scene.OriginalMiss(q,u64(pages)<<20);
        }else{
            count+=scene.OriginalMiss(q,u64(pages)<<20);
        }
        std::atomic_signal_fence(std::memory_order_seq_cst);
    }
    sink=count;
    return std::chrono::duration<double,std::nano>(Clock::now()-begin).count()/N;
}
static double Median(Scene& scene,bool use_filter,unsigned pages){
    double values[7];
    for(auto& v:values)v=Run(scene,use_filter,pages);
    std::sort(std::begin(values),std::end(values));
    return values[3];
}
}
int main(int argc,char**argv){
    Scene scene;
    Verify(scene);
    if(argc>1) {
        puts("image-base bloom filter native correctness: PASS");
        return 0;
    }
    for(const auto pages:{1u,4u,16u}){
        Run(scene,false,pages);Run(scene,true,pages);
        const double a=Median(scene,false,pages),b=Median(scene,true,pages);
        std::printf("real_page_table negative_gpu_write_range=%uMiB original=%.2f ns filtered=%.2f ns saved=%.2f ns/call; at 3000 WRITE bindings/frame = %.3f ms/frame IF all misses\\n",
            pages,a,b,a-b,3000*(a-b)/1e6);
    }
}
