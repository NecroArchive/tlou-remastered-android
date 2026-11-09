#include "../gpu/shadps4/video_core/renderer_vulkan/ring_binding_cursor.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <random>
#include <vector>
struct Binding { unsigned index; std::uint64_t address, size; };
using Cursor=Vulkan::Detail::RingBindingCursor<Binding>;
struct CountedIndex {
    unsigned value;
    static inline unsigned comparisons=0;
    bool operator<(std::size_t rhs) const { ++comparisons; return value<rhs; }
    bool operator==(std::size_t rhs) const { ++comparisons; return value==rhs; }
};
static void CountWork() {
    struct CountedBinding { CountedIndex index; };
    std::array<CountedBinding,40> bindings{};
    for(unsigned i=0;i<40;++i) bindings[i].index.value=i;
    CountedIndex::comparisons=0;
    for(unsigned index=0;index<40;++index) for(const auto& b:bindings) if(b.index==index) break;
    const unsigned old=CountedIndex::comparisons;
    CountedIndex::comparisons=0;
    Vulkan::Detail::RingBindingCursor<CountedBinding> cursor(bindings.data(),bindings.size());
    for(unsigned i=0;i<40;++i) assert(cursor.Take(i)==&bindings[i]);
    assert(old==820 && CountedIndex::comparisons==80);
    std::puts("Ring work gate passed: full 40-buffer packet 820->80 index comparisons");
}
static const Binding* Reference(const std::vector<Binding>& bindings,unsigned index) {
    for(const auto& b:bindings) if(b.index==index) return &b;
    return nullptr;
}
static void Correctness() {
    std::mt19937 rng(19432);
    for(unsigned run=0;run<100000;++run) {
        std::vector<Binding> bindings;
        unsigned buffers=rng()%41;
        for(unsigned i=0;i<buffers;++i) if(rng()%3) bindings.push_back({i,rng(),rng()});
        Cursor cursor(bindings.data(),bindings.size());
        for(unsigned i=0;i<buffers+3;++i) assert(cursor.Take(i)==Reference(bindings,i));
    }
    Cursor empty(nullptr,0); assert(!empty.Take(0)); assert(!empty.Take(40));
    for(unsigned run=0;run<100000;++run) {
        std::vector<Binding> bindings;
        const unsigned buffers=rng()%41;
        for(unsigned i=0;i<buffers;++i) if(rng()%3) bindings.push_back({i,rng(),rng()});
        Vulkan::Detail::RingBindingLookup<Binding> lookup(bindings.data(),bindings.size());
        for(unsigned i=0;i<buffers+3;++i) assert(lookup.Take(i)==Reference(bindings,i));
    }
    std::puts("Ring cursor: 200000 packet comparisons passed; exact pointers, zero/single/sparse/full stages and missing indices");
}
static volatile std::uint64_t sink;
static __attribute__((noinline)) double Measure(const std::vector<Binding>& bindings,unsigned count,bool cursor) {
    constexpr unsigned repeats=200000;
    auto start=std::chrono::steady_clock::now();
    std::uint64_t total=0;
    for(unsigned r=0;r<repeats;++r) {
        // Keep packet data observable at the same boundary as a recorded draw.
        asm volatile("" : : "r"(bindings.data()) : "memory");
        Cursor next(bindings.data(),bindings.size());
        for(unsigned i=0;i<count;++i) {
            const auto* b=cursor?next.Take(i):Reference(bindings,i);
            if(b) total+=b->address;
        }
    }
    sink=total;
    return double(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count())/repeats;
}
static void Benchmark() {
    for(unsigned count:{1,4,8,16,40}) for(unsigned fraction:{0,1,2}) {
        std::vector<Binding> bindings;
        for(unsigned i=0;i<count;++i) if(fraction==2 || (fraction==1 && i%2)) bindings.push_back({i,i*17+3,256});
        std::array<double,7> old{},next{};
        for(unsigned r=0;r<8;++r) {
            double a,b;
            if(r%2) { b=Measure(bindings,count,true); a=Measure(bindings,count,false); }
            else { a=Measure(bindings,count,false); b=Measure(bindings,count,true); }
            if(r) {old[r-1]=a;next[r-1]=b;}
        }
        std::sort(old.begin(),old.end());std::sort(next.begin(),next.end());
        std::printf("buffers=%u ring=%zu old_ns/stage=%.2f new_ns/stage=%.2f saved_ns/stage=%.2f reduction=%.1f%%\n",count,bindings.size(),old[3],next[3],old[3]-next[3],100*(old[3]-next[3])/old[3]);
    }
}
using Adaptive=Vulkan::Detail::RingBindingLookup<Binding>;
static __attribute__((noinline)) double MeasureAdaptive(
                const std::vector<Binding>& bindings,unsigned count,bool adaptive) {
    constexpr unsigned repeats=200000;
    const auto start=std::chrono::steady_clock::now();
    std::uint64_t total=0;
    for(unsigned r=0;r<repeats;++r) {
        asm volatile("" : : "r"(bindings.data()) : "memory");
        Adaptive next(bindings.data(),bindings.size());
        for(unsigned i=0;i<count;++i) {
            const auto* b=adaptive?next.Take(i):Reference(bindings,i);
            if(b) total+=b->address;
        }
    }
    sink=total;
    return double(std::chrono::duration_cast<std::chrono::nanoseconds>(
           std::chrono::steady_clock::now()-start).count())/repeats;
}
static void AdaptiveCost(bool gate) {
    for(unsigned count:{1,4,8,16,40}) for(unsigned fraction:{0,1,2}) {
        std::vector<Binding> bindings;
        for(unsigned i=0;i<count;++i)
            if(fraction==2 || (fraction==1 && i%2))
                bindings.push_back({i,i*17+3,256});
        std::array<double,9> old{},now{};
        for(unsigned i=0;i<9;++i) {
            if(i&1) {
                now[i]=MeasureAdaptive(bindings,count,true);
                old[i]=MeasureAdaptive(bindings,count,false);
            } else {
                old[i]=MeasureAdaptive(bindings,count,false);
                now[i]=MeasureAdaptive(bindings,count,true);
            }
        }
        std::sort(old.begin(),old.end());
        std::sort(now.begin(),now.end());
        const double ratio=now[4]/old[4];
        std::printf("adaptive buffers=%u ring=%zu old_ns/stage=%.2f new_ns/stage=%.2f ratio=%.3f\n",
                    count,bindings.size(),old[4],now[4],ratio);
        // The microbench only models lookup operations. TLOU real drawings
        // typically have 3-5 ring bindings; avoid global regressions at
        // tiny stages and demand large wins for 8+ buffer stages.
        if(gate) {
            const double ceiling=bindings.empty() || bindings.size()==1 ? 1.40 :
                                 count>=16 ? 0.80 : count>=8 ? 0.90 : 1.10;
            if(ratio>ceiling) {
                std::fprintf(stderr,"TLOU adaptive ring lookup cost gate failed: "
                                  "%u slots, %zu ring, ratio %.3f > %.2f\n",
                                  count,bindings.size(),ratio,ceiling);
                std::abort();
            }
        }
    }
}
int main(int argc,char**argv) {
    CountWork(); Correctness();
    if(argc==2 && !std::strcmp(argv[1],"--cost-gate")) {
        AdaptiveCost(true);
    } else if(argc<2 || std::strcmp(argv[1],"--correctness-only")) {
        Benchmark();
        AdaptiveCost(false);
    }
}
