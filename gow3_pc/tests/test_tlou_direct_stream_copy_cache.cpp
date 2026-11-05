// SPDX-License-Identifier: GPL-2.0-or-later
// Exact production direct-mapped copy cache vs the existing unordered_map
// bookkeeping. Not a GPU, copy-worker, or TLOU FPS benchmark.
#include "../gpu/shadps4/video_core/buffer_cache/tlou_direct_stream_copy_cache.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string_view>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <random>
#include <unordered_map>
#include <vector>
using u64=std::uint64_t;
using Direct=VideoCore::DirectStreamCopyCache<8192>;

struct Copy{u64 tick{},epoch{},wraps{},offset{};};
struct Lookup{
    std::unordered_map<u64,Copy> entries;
    std::optional<u64> Find(u64 k,u64 t,u64 e,u64 w) {
        const auto it=entries.find(k);
        if(it!=entries.end()&&it->second.tick==t&&it->second.epoch==e&&it->second.wraps==w)
            return it->second.offset;
        return {};
    }
    void Put(u64 k,u64 t,u64 e,u64 w,u64 off){entries[k]={t,e,w,off};}
};
void Correctness(){
    Direct a;
    Lookup b;
    std::mt19937_64 rng{0xd1a2a541};
    for(unsigned step=0;step<500000;++step){
        const u64 key=rng()%10000;
        const u64 tick=step/10000;
        const u64 epoch=step/200;
        const u64 wraps=step/7000;
        auto got=a.Find(key,tick,epoch,wraps);
        auto ref=b.Find(key,tick,epoch,wraps);
        // Collisions may remove entries, but must never introduce wrong hits.
        if(got) assert(ref && *got==*ref);
        if(rng()%8==0){
            const auto off=rng();
            a.Put(key,tick,epoch,wraps,off);
            b.Put(key,tick,epoch,wraps,off);
        }
    }
    a.Clear();
    for(u64 i=0;i<3000;++i){
        assert(!a.Find(i,0,0,0));
        a.Put(i,0,0,0,i*7);
        assert(!a.Find(i,0,1,0));
        assert(!a.Find(i,1,0,0));
        assert(!a.Find(i,0,0,1));
    }
    a.Clear();
    assert(!a.Find(7,0,0,0));
    a.Put(7,0,0,0,0); // Offset zero is valid, not a miss.
    assert(a.Find(7,0,0,0).has_value());
    assert(*a.Find(7,0,0,0)==0);
}

struct Measurement{double ns{};u64 hits{},misses{};};
enum class Mode{Hash,Direct,Disabled};
volatile u64 sink=0;
Measurement Run(Mode mode,unsigned pct,unsigned frames=80){
    // Repeating N of 4,000 descriptors within the SAME submission. A real
    // game trace is needed before calling any such percentage a TLOU hit rate.
    const unsigned repeated=4000*pct/100;
    auto t0=std::chrono::steady_clock::now();
    auto map=std::make_unique<Lookup>();
    auto cache=std::make_unique<Direct>();
    u64 hits=0,misses=0,total=0;
    for(unsigned frame=0;frame<frames;++frame){
        const u64 tick=frame+1,epoch=frame+1,wraps=frame/25;
        for(unsigned i=0;i<4000;++i){
            const u64 key=(u64(frame)<<40) + u64(i)<<7 ^ (u64(1+i%6)<<44);
            bool hit=false;
            if(mode==Mode::Hash)hit=map->Find(key,tick,epoch,wraps).has_value();
            if(mode==Mode::Direct)hit=cache->Find(key,tick,epoch,wraps).has_value();
            hits+=hit; misses+=!hit;
            if(!hit){
                const u64 offset=total++ * 64;
                if(mode==Mode::Hash)map->Put(key,tick,epoch,wraps,offset);
                if(mode==Mode::Direct)cache->Put(key,tick,epoch,wraps,offset);
            }
        }
        for(unsigned i=0;i<repeated;++i){
            const u64 key=(u64(frame)<<40) + u64(i)<<7 ^ (u64(1+i%6)<<44);
            bool hit=false;
            if(mode==Mode::Hash)hit=map->Find(key,tick,epoch,wraps).has_value();
            if(mode==Mode::Direct)hit=cache->Find(key,tick,epoch,wraps).has_value();
            hits+=hit;misses+=!hit;
            if(!hit){
                const u64 offset=total++ * 64;
                if(mode==Mode::Hash)map->Put(key,tick,epoch,wraps,offset);
                if(mode==Mode::Direct)cache->Put(key,tick,epoch,wraps,offset);
            }
        }
        // The real map is capped at 16,384 and a copy in the next submission
        // invalidates its tick/epoch contract even when key still matches.
        if(map->entries.size()>=16384)map->entries.clear();
    }
    sink=total+hits+misses;
    const double ns=std::chrono::duration<double,std::nano>(
        std::chrono::steady_clock::now()-t0).count()/(4000.0*frames*(1.+pct/100.));
    return {ns,hits,misses};
}
int main(int argc,char**argv){
    Correctness();
    if(argc>1&&std::string_view(argv[1])=="--correctness-only"){
        puts("Direct stream-copy cache stale/epoch/tick/wrap/collision/zero-offset tests: PASS");
        return 0;
    }
    for(unsigned pct:{0u,12u,50u}){
        for(unsigned i=0;i<3;++i){Run(Mode::Hash,pct,8);Run(Mode::Direct,pct,8);}
        std::array<Measurement,7> a{},b{};
        for(unsigned i=0;i<7;++i){
            if(i%2==0){a[i]=Run(Mode::Hash,pct);b[i]=Run(Mode::Direct,pct);}
            else {b[i]=Run(Mode::Direct,pct);a[i]=Run(Mode::Hash,pct);}
        }
        const auto median=[](auto& arr){
            std::sort(arr.begin(),arr.end(),[](auto&x,auto&y){return x.ns<y.ns;});
            return arr[3];
        };
        const auto old=median(a),now=median(b);
        printf("synthetic-%u%% duplicate keys: map=%.3f direct=%.3f saved=%.3f ns/lookup; map_hits=%llu direct_hits=%llu\n",
            pct,old.ns,now.ns,old.ns-now.ns,
            (unsigned long long)old.hits,(unsigned long long)now.hits);
        printf("model only: 4000 lookups/frame => saved=%.4f ms/frame (not measured Android)\n",
            4000*(old.ns-now.ns)/1e6);
    }
}
