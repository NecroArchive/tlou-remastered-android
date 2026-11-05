// SPDX-License-Identifier: GPL-2.0-or-later
// Actual production note/overlap/filter methods, using controlled pipe positions.
#include "../gpu/shadps4/common/types.h"
#include "../gpu/shadps4/video_core/renderer_vulkan/pending_write_filter.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <random>
#include <string>
#include <vector>
namespace Vulkan {
struct State {
    struct PendingWrite {VAddr begin,end;u64 position;bool operator==(const PendingWrite&)const=default;};
    struct Pipe {
        u64 head=128,tail=0;
        u64 Head()const{return head;}
        bool Reached(u64 position)const{return position!=~u64{0}&&position<=tail;}
    } pipe;
    Pipe* draw_pipe=&pipe;
    bool PipeWriteEnd()const{return true;}
    static constexpr u64 PendingUncommitted=~u64{0};
    static constexpr u32 PendingGranuleBits=16,PendingFilterBits=4096;
    std::array<u64,64> pending_filter{};
    u32 pending_hint=0,pending_checks=0;
    VAddr pending_min=~VAddr{0},pending_max=0,copy_min=~VAddr{0},copy_max=0;
    bool noting_packet=true,noting_copy=false,copy_uncommitted=false;
    std::vector<PendingWrite> pending_writes,pending_copy_writes;
    std::vector<u32> uncommitted_indices;
#include "production_pending_writes.inc"
#include "fixtures/pending_writes_v030.inc"
    // The same publication loop as PostDraw, without serializing a GPU packet.
    void Commit() {
        pipe.head+=8;
        for(u32 i:uncommitted_indices)if(pending_writes[i].position==PendingUncommitted)pending_writes[i].position=pipe.head;
        uncommitted_indices.clear();
        if(copy_uncommitted){for(auto& w:pending_copy_writes)if(w.position==PendingUncommitted)w.position=pipe.head;copy_uncommitted=false;}
    }
};
}
static void Compare(const Vulkan::State& a,const Vulkan::State& b) {
    assert(a.pending_filter==b.pending_filter);
    assert(a.pending_writes==b.pending_writes);
    assert(a.pending_copy_writes==b.pending_copy_writes);
    assert(a.uncommitted_indices==b.uncommitted_indices);
    assert(a.pending_hint==b.pending_hint && a.pending_checks==b.pending_checks);
    assert(a.pending_min==b.pending_min && a.pending_max==b.pending_max);
}
static void Correctness() {
    std::mt19937_64 rng(0x19481925);
    for(unsigned i=0;i<50000;++i) {
        Vulkan::State a,b;
        for(auto& word:a.pending_filter)word=rng();
        b.pending_filter=a.pending_filter;
        u64 begin=rng()%(1ull<<40),end=begin+1+rng()%512_MB;
        a.OldFilterPendingRange(begin,end);b.FilterPendingRange(begin,end);
        assert(a.pending_filter==b.pending_filter);
        begin=rng()%(1ull<<40);end=begin+1+rng()%512_MB;
        assert(a.OldPendingFilterHit(begin,end)==b.PendingFilterHit(begin,end));
        a.pending_filter.fill(0);b.pending_filter.fill(0);
        assert(a.OldPendingFilterHit(begin,end)==b.PendingFilterHit(begin,end));
    }
    Vulkan::State a,b;
    for(unsigned i=0;i<10000;++i) {
        const u64 address=rng()%512_MB,size=rng()%32_MB;
        a.noting_copy=b.noting_copy=i%31==0;
        a.OldNotePendingGpuWrite(address,size);b.NotePendingGpuWrite(address,size);Compare(a,b);
        a.Commit();b.Commit();
        if(i%3==0)a.pipe.tail=b.pipe.tail=a.pipe.head-8;
        const u64 at=rng()%512_MB,length=1+rng()%4_MB;
        assert(a.OldPendingWriteOverlaps(at,length)==b.PendingWriteOverlaps(at,length));Compare(a,b);
    }
    for(u64 bit:{0,63,64,4094,4095,4096,8191})for(u64 span:{1,2,63,64,65,4096,4097}) {
        Vulkan::State a,b;const u64 start=(bit<<16)+65535,end=start+(span<<16);
        a.OldFilterPendingRange(start,end);b.FilterPendingRange(start,end);Compare(a,b);
    }
    std::puts("Pending writes: 50000 mask/query cases + 10000 complete note/commit/overlap sequences passed, including hash wrap, saturation, pruning and copy writes");
}
static volatile u64 sink;
static __attribute__((noinline)) double Measure(bool legacy,u64 size,bool query_miss) {
    Vulkan::State s;
    constexpr unsigned iterations=200000;
    u64 sum=0;
    auto begin=std::chrono::steady_clock::now();
    for(unsigned i=0;i<iterations;++i) {
        if(query_miss)sum+=legacy?s.OldPendingFilterHit(0x1000000000,size+0x1000000000):s.PendingFilterHit(0x1000000000,size+0x1000000000);
        else {
            if(legacy)s.OldNotePendingGpuWrite(0x1000000000,size);
            else s.NotePendingGpuWrite(0x1000000000,size);
            s.Commit();
        }
        asm volatile("" : : "r"(&s) : "memory");
    }
    sink=sum+s.pending_writes.size();
    return double(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count())/iterations;
}
static void Benchmark() {
    for(u64 size:{256ull,1_MB,9_MB,32_MB,256_MB})for(bool miss:{false,true}) {
        std::array<double,7> old{},next{};
        for(unsigned i=0;i<8;++i) {
            double a,b;
            if(i%2){b=Measure(false,size,miss);a=Measure(true,size,miss);}
            else{a=Measure(true,size,miss);b=Measure(false,size,miss);}
            if(i){old[i-1]=a;next[i-1]=b;}
        }
        std::sort(old.begin(),old.end());std::sort(next.begin(),next.end());
        const double delta=old[3]-next[3];
        std::printf("pending-%s bytes=%llu old_ns=%.2f new_ns=%.2f saved_ns=%.2f scenario_command_cpu_ms_frame_1000_calls=%.4f\n",
                    miss?"empty-query":"note-commit",(unsigned long long)size,old[3],next[3],delta,delta/1000);
    }
}
int main(int argc,char**argv) {
    Correctness();
    if(argc<2 || std::string(argv[1])!="--correctness-only")Benchmark();
}
