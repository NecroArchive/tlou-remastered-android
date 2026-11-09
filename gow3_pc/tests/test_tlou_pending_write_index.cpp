// SPDX-License-Identifier: GPL-2.0-or-later
// Production index vs the old cyclic search, with identical update/commit
// semantics. All interval/filter/selection state is compared, not just hits.
#include "video_core/renderer_vulkan/tlou_pending_write_index.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string_view>
#include <vector>

namespace {
using u32=std::uint32_t;
using u64=std::uint64_t;
constexpr u32 Shift=16, FilterBits=4096;
constexpr u64 Uncommitted=~u64{0};
struct Write {u64 begin,end,position;bool operator==(const Write&)const=default;};
struct Event {u64 address,size;bool pending;};
struct Model {
    std::vector<Write> writes;
    std::vector<u32> uncommitted;
    Vulkan::Detail::PendingWriteIndex<Write> index{};
    std::array<u64,FilterBits/64> filter{};
    u64 min=~u64{0},max=0,tick=0;
    u64 checks=0,marks=0,hint_hits=0,walks=0;
    u32 hint=0;

    void Mark(u64 lo,u64 hi) {
        ++marks;
        u64 first=lo>>Shift,last=(hi-1)>>Shift;
        if(last-first>=FilterBits){filter.fill(~u64{0});return;}
        for(u64 page=first;page<=last;++page) {
            u32 bit=u32(page%FilterBits);
            filter[bit/64]|=u64{1}<<(bit%64);
        }
    }
    bool FilterHit(u64 lo,u64 hi) {
        ++checks;
        u64 first=lo>>Shift,last=(hi-1)>>Shift;
        if(last-first>=FilterBits)return true;
        for(u64 page=first;page<=last;++page) {
            u32 bit=u32(page%FilterBits);
            if((filter[bit/64]>>(bit%64))&1)return true;
        }
        return false;
    }
    u64 skipped_rebuilds=0;
    void Retire(u64 before,bool skip_unchanged=false) {
        const auto old_count=writes.size();
        std::erase_if(writes,[&](const Write& w){return w.position!=Uncommitted&&w.position<=before;});
        if(old_count==writes.size()&&skip_unchanged){
            ++skipped_rebuilds;
            return; // production fast path: all filter/index/bounds state is unchanged
        }
        if(old_count!=writes.size())index.Rebuild(writes);
        min=~u64{0};max=0;filter.fill(0);
        for(const auto& w:writes){min=std::min(min,w.begin);max=std::max(max,w.end);Mark(w.begin,w.end);}
    }
    void Note(u64 lo,u64 size,bool pending,bool optimized) {
        const u64 hi=lo+size,pos=pending?Uncommitted:++tick;
        const auto update=[&](u32 i) {
            auto& w=writes[i];
            const bool covered=Vulkan::Detail::PendingWriteAlreadyCovered(w,lo,hi);
            hint=i;w.begin=std::min(w.begin,lo);w.end=std::max(w.end,hi);
            w.position=pos;min=std::min(min,lo);max=std::max(max,hi);
            if(optimized&&!covered)index.Set(i,w);
            if(!optimized||!covered)Mark(lo,hi);
            if(pending)uncommitted.push_back(i);
        };
        const u32 count=FilterHit(lo,hi)?u32(writes.size()):0;
        if(optimized&&count&&Vulkan::Detail::PendingWriteHintOverlaps(writes,hint,lo,hi)) {
            ++hint_hits;update(hint);return;
        }
        if(optimized&&index.CanQuery(count)) {
            if(auto found=index.FirstOverlap(writes,hint,lo,hi)) {update(*found);return;}
        } else {
            const u32 first=hint<count?hint:0;
            for(u32 step=0;step<count;++step) {
                const u32 i=first+step<count?first+step:first+step-count;
                ++walks;
                const auto& w=writes[i];
                if(lo<=w.end&&w.begin<=hi){update(i);return;}
            }
        }
        if(pending)uncommitted.push_back(u32(writes.size()));
        writes.push_back({lo,hi,pos});
        if(optimized)index.Set(writes.size()-1,writes.back());
        min=std::min(min,lo);max=std::max(max,hi);Mark(lo,hi);
    }
    bool Equals(const Model& rhs)const {
        return writes==rhs.writes&&uncommitted==rhs.uncommitted&&
            filter==rhs.filter&&min==rhs.min&&max==rhs.max&&hint==rhs.hint&&tick==rhs.tick;
    }
};
// Compare every live GPU-write overlap with the exact Stage-A read predicate:
 // strictly overlapping half-open intervals, non-retired positions, including
 // entries still uncommitted. Read-side queries must never return a false
 // negative, since that would snapshot stale guest constants.
bool LegacyLive(const std::vector<Write>& writes,u64 begin,u64 end,u64 retired) {
    for(const auto& w:writes) {
        if(begin<w.end && w.begin<end &&
           (w.position==Uncommitted || w.position>retired))return true;
    }
    return false;
}
bool IndexedLive(const Model& model,u64 begin,u64 end,u64 retired) {
    if(!model.index.CanQueryLive(model.writes.size()))
        return LegacyLive(model.writes,begin,end,retired);
    return model.index.AnyLiveOverlap(model.writes,begin,end,[retired](const Write& w) {
        return w.position==Uncommitted || w.position>retired;
    });
}
void Verify(){
    Model reference,optimized;
    std::mt19937_64 rng(0x246ed19);
    std::array<u64,256> hot{};
    u64 current=0;
    for(u32 i=0;i<180000;++i){
        if(i&&i%67==0){
            reference.uncommitted.clear();optimized.uncommitted.clear();
            for(auto& w:reference.writes)if(w.position==Uncommitted)w.position=reference.tick;
            for(auto& w:optimized.writes)if(w.position==Uncommitted)w.position=optimized.tick;
        }
        if(i&&i%709==0){
            const auto cut=reference.tick-24;
            reference.Retire(cut);optimized.Retire(cut,true);
        }
        if(i%7==0)current=rng()%hot.size();
        if(!hot[current])hot[current]=(rng()%8192)*65536+0x1000000;
        const u64 lo=hot[current]+((rng()%8)==0?rng()%128:0);
        const u64 size=1+(rng()%4)*4096;
        const bool pending=rng()%13==0;
        reference.Note(lo,size,pending,false);
        optimized.Note(lo,size,pending,true);
        if(i%307==0)assert(reference.Equals(optimized));
        if(i%113==0) {
            for(int q=0;q<12;++q) {
                const u64 base=hot[rng()%hot.size()];
                const u64 read=base?base+((rng()%5)==0?65536u:rng()%8192):0x7ffff000u;
                const u64 size=1+rng()%65536;
                const u64 retired=optimized.tick>30 ? optimized.tick-(rng()%31):0;
                assert(LegacyLive(reference.writes,read,read+size,retired)==
                       IndexedLive(optimized,read,read+size,retired));
            }
        }
    }
    assert(reference.Equals(optimized));
    assert(optimized.hint_hits>20000);
    assert(optimized.marks<reference.marks);
    assert(optimized.checks==reference.checks);
    // Repeated pruning before Stage B advances must not mutate the live
    // ranges, even when the optimized path skips filter rebuilding.
    Model idle_old,idle_new;
    for(u32 i=0;i<256;++i) {
        const u64 address=0x40000000+u64(i)*0x10000;
        idle_old.Note(address,8192,false,false);
        idle_new.Note(address,8192,false,true);
    }
    for(u32 i=0;i<2000;++i) {
        idle_old.Retire(0);
        idle_new.Retire(0,true);
        assert(idle_old.Equals(idle_new));
    }
    assert(idle_new.skipped_rebuilds==2000);
    // A completion snapshot can conservatively observe an older value,
    // but must not overlook any still-live write at a newer completion.
    for(u64 completed=0;completed<255;++completed){
        for(u64 next=completed;next<256;next+=7) {
            for(u32 i=0;i<8;++i) {
                const u64 address=0x40000000+u64(i*31)*0x10000;
                const bool fresh=LegacyLive(idle_old.writes,address,address+8192,next);
                const bool snap=LegacyLive(idle_new.writes,address,address+8192,completed);
                assert(!fresh||snap);
            }
        }
    }
    // The original bloom filter ignores adjacent ranges separated exactly
    // at a 64 KiB boundary if it has not marked the next window. A spatial
    // index must reproduce this decision even if its own inclusive index sees
    // the neighboring interval.
    Model edge_old,edge_new;
    for(u32 i=0;i<70;++i){
        const u64 at=0x30000000+u64(i)*65536;
        edge_old.Note(at,65536,false,false);
        edge_new.Note(at,65536,false,true);
        assert(edge_old.Equals(edge_new));
    }
    // >2048 active entries, then a vector compaction/rebuild.
    Model a,b;
    for(u32 i=0;i<2400;++i){
        const u64 lo=0x10000000+u64(i)*0x20000;
        a.Note(lo,65536,false,false);b.Note(lo,65536,false,true);
    }
    assert(a.Equals(b)); // >2048 ranges falls back to original traversal
    a.Note(0x10000000+65536,128,false,false);
    b.Note(0x10000000+65536,128,false,true);
    assert(a.Equals(b));
    a.Retire(a.tick-1300);b.Retire(b.tick-1300,true);
    assert(a.Equals(b)); // after vector compaction, index must be rebuilt
    for(u32 i=0;i<1000;++i){
        const u64 lo=0x10000000+u64(i%32)*0x20000;
        a.Note(lo,32768,false,false);b.Note(lo,32768,false,true);
        if(i%29==0)assert(a.Equals(b));
    }
    assert(a.Equals(b));
    for(u32 i=0;i<4096;++i) {
        const u64 lo=0x10000000+u64(i%2400)*0x20000;
        const u64 retired=b.tick>u64(i%512)?b.tick-(i%512):0;
        assert(LegacyLive(a.writes,lo,lo+65536,retired)==
               IndexedLive(b,lo,lo+65536,retired));
    }
    std::printf("pending index correctness: 180000 randomized writes and 2400-range overflow, inclusive boundaries, retirement/reindex passed; skipped duplicate filter markings=%llu\n",
                (unsigned long long)(reference.marks-optimized.marks));
}
std::vector<Event> Workload(int reuse){
    std::mt19937_64 rng(943u+reuse);
    std::vector<Event> result;result.reserve(240000);
    u64 current=0;
    for(int i=0;i<240000;++i){
        if(rng()%100>=u64(reuse))current=rng()%120;
        result.push_back({0x2000000+current*65536,(rng()%8==0)?512u:4096u,false});
    }
    return result;
}
// 512/1024 pending writes, with most read-only constant buffers not
// touching in-flight writes: mirrors the expensive RingBindings false path.
// Exact predicates (including fence retirement) are compared at each step.
static volatile u64 sink;
void BenchmarkLive(unsigned n){
    Model model;
    for(unsigned i=0;i<n;++i) {
        const u64 a=0x20000000+u64(i)*0x10000;
        model.Note(a,8192,false,true);
    }
    const u64 retired=model.tick/2;
    struct Query {u64 begin,end;};
    std::vector<Query> queries;queries.reserve(280000);
    for(u32 i=0;i<280000;++i) {
        const u64 slot=(u64(i)*3571u)%n;
        const u64 start=0x20000000+slot*0x10000+
              ((i%7==0)?4096u:(i%7==1)?8192u:32768u);
        queries.push_back({start,start+1024u});
    }
    for(const auto& q:queries) {
        assert(LegacyLive(model.writes,q.begin,q.end,retired)==
               IndexedLive(model,q.begin,q.end,retired));
    }
    auto run=[&](bool indexed) {
        u64 sum=0;
        auto t0=std::chrono::steady_clock::now();
        for(const auto& q:queries) {
            sum+=indexed?IndexedLive(model,q.begin,q.end,retired):
                          LegacyLive(model.writes,q.begin,q.end,retired);
        }
        sink=sink+sum;
        return std::chrono::duration<double,std::milli>(
             std::chrono::steady_clock::now()-t0).count();
    };
    std::array<double,7> before{},after{};
    for(size_t i=0;i<before.size();++i){
        if(i%2){after[i]=run(true);before[i]=run(false);}
        else{before[i]=run(false);after[i]=run(true);}
    }
    std::sort(before.begin(),before.end());std::sort(after.begin(),after.end());
    std::printf("pending live-read %u ranges, 280000 queries: old=%.3fms indexed=%.3fms ratio=%.3f (host, not FPS)\n",
                n,before[3],after[3],after[3]/before[3]);
}
double Run(const std::vector<Event>& events,bool indexed){
    Model state;
    const auto start=std::chrono::steady_clock::now();
    for(const auto& e:events)state.Note(e.address,e.size,e.pending,indexed);
    sink=sink+state.walks+state.checks+state.marks;
    return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
}
void Cost(int reuse){
    auto events=Workload(reuse);
    std::array<double,7> a{},b{};
    for(size_t i=0;i<a.size();++i){
        if(i&1){b[i]=Run(events,true);a[i]=Run(events,false);}
        else{a[i]=Run(events,false);b[i]=Run(events,true);}
    }
    std::sort(a.begin(),a.end());std::sort(b.begin(),b.end());
    std::printf("pending index %d%% repeat, 240000 writes: original=%.3fms indexed=%.3fms ratio=%.3f (host, not FPS)\n",
                reuse,a[3],b[3],b[3]/a[3]);
}
}
int main(int argc,char** argv){
    Verify();
    if(argc==1){Cost(90);Cost(70);Cost(40);BenchmarkLive(512);BenchmarkLive(1024);return 0;}
    return argc==2&&std::string_view(argv[1])=="--correctness-only"?0:2;
}
