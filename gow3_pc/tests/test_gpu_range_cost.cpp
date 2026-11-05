// SPDX-License-Identifier: GPL-2.0-or-later
#include "../gpu/shadps4/common/types.h"
#include "../gpu/shadps4/video_core/buffer_cache/gpu_window_bits.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
namespace VideoCore {
#include "production_range_set.inc"
template<u64 Bits=1<<14> struct Registry {
    static constexpr u64 GpuWindowBits=Bits;
    std::unique_ptr<std::atomic<u64>[]> gpu_window_bits{new std::atomic<u64>[Bits/64]{}};
    std::shared_mutex gpu_ranges_mutex;
    RangeSet gpu_modified_ranges;
    struct Entry {VAddr address=0;u64 size=0,tick=0;};
    static constexpr u32 GpuAddCacheSize=512;
    std::array<Entry,GpuAddCacheSize> gpu_add_cache{};
    std::unordered_map<u64,u64> gpu_write_window_tick,gpu_write_page_tick;
    struct Scheduler {u64 tick=1;u64 CurrentTick()const{return tick;}} scheduler;
    u64 RangeTick()const{return scheduler.CurrentTick();}
#include "production_gpu_ranges.inc"
#include "fixtures/gpu_ranges_add_v030.inc"
};
}
struct CountedWord {
    u64 value=0;
    static inline u64 reads=0,writes=0;
    u64 load(std::memory_order)const{++reads;return value;}
    void fetch_or(u64 mask,std::memory_order){++writes;value|=mask;}
};
template<class Word> void OldMark(Word* words,u64 first,u64 last) {
    for(u64 w=first;w<=last;++w) {
        auto& word=words[w>>6];const u64 bit=1ull<<(w&63);
        if(!(word.load(std::memory_order_relaxed)&bit))word.fetch_or(bit,std::memory_order_relaxed);
    }
}
static void Correctness() {
    std::mt19937_64 rng(0x19481924);
    for(unsigned attempt=0;attempt<10000;++attempt) {
        std::array<CountedWord,32> old{},next{};
        for(unsigned i=0;i<32;++i)old[i].value=next[i].value=rng();
        u64 first=rng()%2048,last=rng()%2048;if(first>last)std::swap(first,last);
        OldMark(old.data(),first,last);
        VideoCore::Detail::MarkGpuWindowBits(next.data(),first,last);
        for(unsigned i=0;i<32;++i)assert(old[i].value==next[i].value);
    }
    std::array<CountedWord,8> old{},next{};
    CountedWord::reads=CountedWord::writes=0;OldMark(old.data(),0,511);
    assert(CountedWord::reads==512 && CountedWord::writes==512);
    CountedWord::reads=CountedWord::writes=0;
    VideoCore::Detail::MarkGpuWindowBits(next.data(),0,511);
    assert(CountedWord::reads==8 && CountedWord::writes==8);
    VideoCore::Registry<> a,b;
    for(unsigned step=0;step<10000;++step) {
        const u64 address=rng()%(800_MB);
        const u64 size=rng()%32_MB;
        if(step%17==0) {a.GpuRangesSubtract(address,size);b.GpuRangesSubtract(address,size);}
        else {a.LegacyAdd(address,size);b.GpuRangesAdd(address,size);}
        if(step%19==0){++a.scheduler.tick;++b.scheduler.tick;}
        for(unsigned i=0;i<(1<<14)/64;++i)assert(a.gpu_window_bits[i].load()==b.gpu_window_bits[i].load());
        assert(a.gpu_modified_ranges.Count()==b.gpu_modified_ranges.Count());
        a.gpu_modified_ranges.ForEach([&](u64 lo,u64 hi){assert(b.gpu_modified_ranges.Contains(lo,hi-lo));});
        assert(a.gpu_write_window_tick==b.gpu_write_window_tick);
        assert(a.gpu_write_page_tick==b.gpu_write_page_tick);
        for(unsigned i=0;i<512;++i) {
            assert(a.gpu_add_cache[i].address==b.gpu_add_cache[i].address);
            assert(a.gpu_add_cache[i].size==b.gpu_add_cache[i].size);
            assert(a.gpu_add_cache[i].tick==b.gpu_add_cache[i].tick);
        }
    }
    std::array<std::atomic<u64>,8> shared{};
    std::vector<std::thread> writers;
    for(u64 t=0;t<8;++t)writers.emplace_back([&,t]{
        for(u64 i=t;i<512;i+=8)VideoCore::Detail::MarkGpuWindowBits(shared.data(),i,i);
    });
    for(auto& thread:writers)thread.join();
    for(auto& word:shared)assert(word.load()==~u64{0});
    std::puts("GPU ranges: 10000 mask cases + 10000 complete registry sequences passed; cache, subtract, ticks, byte ranges and concurrent same-word writers preserved");
    std::puts("Work gate: 32 MiB aligned first marking: 512->8 atomic loads and 512->8 atomic RMW operations");
}
static volatile u64 sink;
static __attribute__((noinline)) double Measure(bool legacy,u64 size,bool advance_tick) {
    VideoCore::Registry<(1<<24)> registry;
    registry.GpuRangesAdd(0x1004000000,size);
    constexpr unsigned iterations=100000;
    auto start=std::chrono::steady_clock::now();
    for(unsigned i=0;i<iterations;++i) {
        if(advance_tick)++registry.scheduler.tick;
        if(legacy)registry.LegacyAdd(0x1004000000,size);
        else registry.GpuRangesAdd(0x1004000000,size);
    }
    const auto elapsed=std::chrono::steady_clock::now()-start;
    sink=registry.gpu_modified_ranges.Count()+registry.gpu_write_window_tick.size();
    return double(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count())/iterations;
}
static void Benchmark() {
    for(u64 size:{256ull,4_KB,1_MB,9_MB,32_MB})for(bool tick:{false,true}) {
        std::array<double,7> old{},next{};
        for(unsigned i=0;i<8;++i) {
            double a,b;
            if(i%2){b=Measure(false,size,tick);a=Measure(true,size,tick);}
            else{a=Measure(true,size,tick);b=Measure(false,size,tick);}
            if(i){old[i-1]=a;next[i-1]=b;}
        }
        std::sort(old.begin(),old.end());std::sort(next.begin(),next.end());
        const double delta=old[3]-next[3];
        std::printf("full-add bytes=%llu tick=%s old_ns=%.2f new_ns=%.2f saved_ns=%.2f scenario_cpu_ms_frame_1000_writes=%.4f\n",
                    (unsigned long long)size,tick?"advance":"same",old[3],next[3],delta,delta/1000);
    }
}
int main(int argc,char**argv) {
    Correctness();
    if(argc<2 || std::string(argv[1])!="--correctness-only")Benchmark();
}
