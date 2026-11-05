// SPDX-License-Identifier: GPL-2.0-or-later
// Production word-mask helpers vs the original individual-bit loops.
// All results are CPU primitive costs, not TLOU packet counts or device FPS.
#include "../gpu/shadps4/video_core/renderer_vulkan/tlou_write_range_bits.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <thread>
#include <vector>

namespace {
using u64=std::uint64_t;
constexpr std::size_t FilterBits=4096, WindowBits=16384;
using Filter=std::array<u64,FilterBits/64>;
using Windows=std::array<std::atomic<u64>,WindowBits/64>;

void OldSet(Filter& out,u64 first,u64 last) {
    if(last-first>=FilterBits){out.fill(~u64{0});return;}
    for(u64 g=first;g<=last;++g){const unsigned bit=g%FilterBits;out[bit/64]|=u64{1}<<(bit%64);}
}
bool OldHit(const Filter& out,u64 first,u64 last) {
    if(last-first>=FilterBits)return true;
    for(u64 g=first;g<=last;++g){const unsigned bit=g%FilterBits;
        if(out[bit/64]&(u64{1}<<(bit%64)))return true;}
    return false;
}
void OldAtomicMark(Windows& out,u64 first,u64 last) {
    for(u64 g=first;g<=last;++g){auto& word=out[g/64];const u64 bit=u64{1}<<(g%64);
        if(!(word.load(std::memory_order_relaxed)&bit))
            word.fetch_or(bit,std::memory_order_relaxed);}
}
void Clear(Windows& out){for(auto& x:out)x.store(0,std::memory_order_relaxed);}
bool Equal(const Windows& a,const Windows& b) {
    for(std::size_t i=0;i<a.size();++i)
        if(a[i].load(std::memory_order_relaxed)!=b[i].load(std::memory_order_relaxed))return false;
    return true;
}

void Correctness() {
    Filter old{},now{};
    std::mt19937_64 rng{0x5ac7e17};
    const std::array<u64,24> lengths{1,2,3,4,31,32,63,64,65,127,128,
           255,256,257,511,512,513,1023,1024,2047,4095,4096,4097,8192};
    for(u64 length:lengths)
        for(u64 first: {0ull,1ull,62ull,63ull,64ull,4094ull,4095ull,4096ull,
                        8191ull,65535ull,1048583ull}) {
            old.fill(0);now.fill(0);
            OldSet(old,first,first+length-1);
            Vulkan::Detail::SetPendingRangeBits(now,first,first+length-1);
            assert(old==now);
            for(u64 q:lengths) {
                const u64 at=(first+4095)%8192;
                assert(OldHit(old,at,at+q-1)==
                       Vulkan::Detail::PendingRangeBitsMayHit(now,at,at+q-1));
            }
        }
    for(int k=0;k<150000;++k) {
        const u64 first=rng()%(1ull<<24);
        const u64 length=lengths[rng()%lengths.size()];
        OldSet(old,first,first+length-1);
        Vulkan::Detail::SetPendingRangeBits(now,first,first+length-1);
        assert(old==now);
        const u64 query=rng()%(1ull<<24);
        const u64 qlen=lengths[rng()%lengths.size()];
        assert(OldHit(old,query,query+qlen-1)==
               Vulkan::Detail::PendingRangeBitsMayHit(now,query,query+qlen-1));
        if((k&1023)==0){old.fill(0);now.fill(0);}
    }
    Windows a{},b{};
    for(u64 length:lengths) {
        if(length>WindowBits)continue;
        for(u64 first : {0ull,1ull,63ull,64ull,4095ull,4096ull,8190ull}) {
            if(first+length>WindowBits)continue;
            Clear(a);Clear(b);
            OldAtomicMark(a,first,first+length-1);
            Vulkan::Detail::MarkGpuWriteWindowBits(b.data(),first,first+length-1);
            assert(Equal(a,b));
        }
    }
    Clear(a);Clear(b);
    for(int k=0;k<100000;++k) {
        const u64 first=rng()%WindowBits;
        const u64 length=1+rng()%std::min<u64>(2048,WindowBits-first);
        OldAtomicMark(a,first,first+length-1);
        Vulkan::Detail::MarkGpuWriteWindowBits(b.data(),first,first+length-1);
    }
    assert(Equal(a,b));
    // A concurrent set cannot lose bits with relaxed fetch_or; compare to
    // sequential reference after all workers have joined.
    Clear(a);Clear(b);
    std::array<std::thread,4> threads;
    for(unsigned id=0;id<threads.size();++id) {
        threads[id]=std::thread([&b,id]{
            for(unsigned k=0;k<5000;++k) {
                const u64 first=(id*4001ull+k*17ull)%(WindowBits-513);
                Vulkan::Detail::MarkGpuWriteWindowBits(b.data(),first,first+512);
            }
        });
    }
    for(auto& t:threads)t.join();
    for(unsigned id=0;id<threads.size();++id)
        for(unsigned k=0;k<5000;++k){
            const u64 first=(id*4001ull+k*17ull)%(WindowBits-513);
            OldAtomicMark(a,first,first+512);
        }
    assert(Equal(a,b));
}

enum class Variant {Old,New};
double PendingCost(u64 len,Variant v) {
    Filter bits{};u64 sink=0;
    constexpr unsigned N=20000;
    auto start=std::chrono::steady_clock::now();
    for(unsigned i=0;i<N;++i) {
        const u64 first=65536ull+(i*17ull)%4096;
        if(v==Variant::Old)OldSet(bits,first,first+len-1);
        else Vulkan::Detail::SetPendingRangeBits(bits,first,first+len-1);
        sink+=OldHit(bits,first,first); // same read, outside measured difference
        std::atomic_signal_fence(std::memory_order_seq_cst);
    }
    assert(sink==N);
    return std::chrono::duration<double,std::nano>(
            std::chrono::steady_clock::now()-start).count()/N;
}
double AtomicCost(u64 len,Variant v) {
    Windows bits{};u64 sink=0;
    constexpr unsigned N=20000;
    auto start=std::chrono::steady_clock::now();
    for(unsigned i=0;i<N;++i) {
        const u64 first=(i*17ull)%(WindowBits-len);
        if(v==Variant::Old)OldAtomicMark(bits,first,first+len-1);
        else Vulkan::Detail::MarkGpuWriteWindowBits(bits.data(),first,first+len-1);
        sink+=bits[first/64].load(std::memory_order_relaxed)!=0;
        std::atomic_signal_fence(std::memory_order_seq_cst);
    }
    assert(sink==N);
    return std::chrono::duration<double,std::nano>(
            std::chrono::steady_clock::now()-start).count()/N;
}
void Benchmark(const char* name,u64 granules, double (*measure)(u64,Variant)) {
    std::array<double,9> old{},now{};
    for(int w=0;w<3;++w){measure(granules,Variant::Old);measure(granules,Variant::New);}
    for(unsigned k=0;k<old.size();++k) {
        if(k%2==0) {
            old[k]=measure(granules,Variant::Old);now[k]=measure(granules,Variant::New);
        } else {
            now[k]=measure(granules,Variant::New);old[k]=measure(granules,Variant::Old);
        }
    }
    std::sort(old.begin(),old.end());std::sort(now.begin(),now.end());
    std::printf("%s len=%llu windows old=%.2f new=%.2f delta=%.2f ns/operation\n",
        name,(unsigned long long)granules,old[4],now[4],old[4]-now[4]);
}
}

int main(int argc,char** argv) {
    Correctness();
    if(argc>1 && std::strcmp(argv[1],"--correctness-only")==0) {
        std::puts("GPU-written and pending range equivalence/concurrency: PASS");
        return 0;
    }
    Benchmark("pending-mark",1,PendingCost);
    Benchmark("pending-mark",512,PendingCost);
    Benchmark("gpu-atomic-mark",1,AtomicCost);
    Benchmark("gpu-atomic-mark",512,AtomicCost);
    std::puts("512 windows = 32 MiB of *address coverage*, not bytes physically copied.");
    std::puts("For frame-time estimates multiply per-operation delta by the real in-game size histogram; do not sum separate thread times.");
}
