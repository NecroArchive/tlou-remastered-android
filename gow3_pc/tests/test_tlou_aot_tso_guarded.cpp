// SPDX-License-Identifier: GPL-2.0-or-later
// ARM64 screening only: actual x86-TSO requires multicore litmus validation.
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#if !defined(__aarch64__)
#error "ARM64-only candidate"
#endif
constexpr unsigned N=600000;
inline void Fence() { asm volatile("dmb ish" ::: "memory"); }
inline uint64_t RawRead(const void* p,unsigned n) {
    uint64_t x=0; std::memcpy(&x,p,n); return x;
}
inline void RawWrite(void* p,uint64_t v,unsigned n) { std::memcpy(p,&v,n); }
__attribute__((noinline)) uint64_t OriginalLoad(const void* p,unsigned n) {
    Fence();uint64_t value=RawRead(p,n);Fence();return value;
}
__attribute__((noinline)) void OriginalStore(void* p,uint64_t v,unsigned n) {
    Fence();RawWrite(p,v,n);Fence();
}
__attribute__((noinline)) uint64_t GuardedLoad(const void* p,unsigned n) {
    if((reinterpret_cast<uintptr_t>(p)&(n-1))==0) {
        switch(n) {
            case 1: return __atomic_load_n((const uint8_t*)p,__ATOMIC_ACQUIRE);
            case 2: return __atomic_load_n((const uint16_t*)p,__ATOMIC_ACQUIRE);
            case 4: return __atomic_load_n((const uint32_t*)p,__ATOMIC_ACQUIRE);
            case 8: return __atomic_load_n((const uint64_t*)p,__ATOMIC_ACQUIRE);
        }
    }
    return OriginalLoad(p,n);
}
__attribute__((noinline)) void GuardedStore(void* p,uint64_t v,unsigned n) {
    if((reinterpret_cast<uintptr_t>(p)&(n-1))==0) {
        switch(n) {
            case 1: __atomic_store_n((uint8_t*)p,(uint8_t)v,__ATOMIC_RELEASE);return;
            case 2: __atomic_store_n((uint16_t*)p,(uint16_t)v,__ATOMIC_RELEASE);return;
            case 4: __atomic_store_n((uint32_t*)p,(uint32_t)v,__ATOMIC_RELEASE);return;
            case 8: __atomic_store_n((uint64_t*)p,(uint64_t)v,__ATOMIC_RELEASE);return;
        }
    }
    OriginalStore(p,v,n);
}
struct Result {double ns;uint64_t sum;std::array<uint8_t,32> bytes;};
Result Run(bool guarded,unsigned bytes,unsigned misalignPercent) {
    alignas(64) std::array<uint8_t,32> source{},target{};
    for(unsigned i=0;i<source.size();++i)source[i]=uint8_t(i*13+7);
    uint64_t sum=0;
    const auto t0=std::chrono::steady_clock::now();
    for(unsigned i=0;i<N;++i) {
        const bool misalign=(i%100)<misalignPercent;
        const unsigned off=misalign?1:0;
        const uint64_t value=guarded?GuardedLoad(source.data()+off,bytes)
                                    :OriginalLoad(source.data()+off,bytes);
        if(guarded)GuardedStore(target.data()+off,value^i,bytes);
        else OriginalStore(target.data()+off,value^i,bytes);
        sum+=value;
    }
    const auto t1=std::chrono::steady_clock::now();
    return {std::chrono::duration<double,std::nano>(t1-t0).count()/N,
            sum,target};
}
int main() {
    for(unsigned n:{1u,2u,4u,8u})for(unsigned unaligned:{0u,10u,50u}) {
        std::array<std::vector<double>,2> timings;
        for(unsigned trial=0;trial<10;++trial) {
            const auto baseline=Run(false,n,unaligned);
            const auto optimized=Run(true,n,unaligned);
            assert(baseline.sum==optimized.sum);
            assert(baseline.bytes==optimized.bytes);
            if(trial>=2) {
                timings[0].push_back(baseline.ns);
                timings[1].push_back(optimized.ns);
            }
        }
        for(auto& xs:timings)std::sort(xs.begin(),xs.end());
        const double old=timings[0][timings[0].size()/2];
        const double next=timings[1][timings[1].size()/2];
        std::printf("TLOU ARM64 TSO GUARDED size=%u unaligned=%u%% full=%.2fns aligned-ar=%.2fns ratio=%.3f checksum PASS\n",
                    n,unaligned,old,next,next/old);
    }
}
