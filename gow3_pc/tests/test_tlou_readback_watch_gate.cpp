// SPDX-License-Identifier: GPL-2.0-or-later
// AOT TLOU: GPU readback watcher sweep must retain exact submission semantics.
#include "../gpu/shadps4/video_core/buffer_cache/tlou_readback_watch_gate.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>
struct R {std::uint64_t tick; bool watched=false;};
static void sweep(std::vector<R>& v,std::uint64_t now) {
    for(auto& x:v) if(!x.watched && x.tick < now) x.watched=true;
}
int main() {
    VideoCore::Detail::TlouReadbackWatchSweepGate gate;
    std::vector<R> baseline, fast;
    std::uint64_t legacy_visits=0, fast_visits=0;
    // 240 submissions, 600 draw polls between them, 3 async readbacks each.
    // New readbacks get exactly the CurrentTick() at queue time.
    for(std::uint64_t tick=20;tick<260;++tick) {
        for(int event=0;event<600;++event) {
            if(event==0 || event==170 || event==499) {
                baseline.push_back({tick});fast.push_back({tick});
            }
            legacy_visits+=baseline.size();
            sweep(baseline,tick);
            if(gate.ShouldSweep(tick)) {
                fast_visits+=fast.size();
                sweep(fast,tick);
            }
            assert(baseline.size()==fast.size());
            for(size_t i=0;i<baseline.size();++i) assert(baseline[i].watched==fast[i].watched);
            if(event%100==0 && baseline.size()>40) {
                baseline.erase(baseline.begin(),baseline.begin()+3);
                fast.erase(fast.begin(),fast.begin()+3);
            }
        }
    }
    // Previously queued readbacks become protected at the next tick.
    assert(fast_visits < legacy_visits/100);
    assert(!gate.ShouldSweep(259));
    assert(gate.ShouldSweep(260));
    std::printf("TLOU readback sweep: comparisons %llu -> %llu (%.2f%% fewer); exact watch state, poll/landing untouched\n",
        (unsigned long long)legacy_visits,(unsigned long long)fast_visits,
        100.0*(1.0-double(fast_visits)/double(legacy_visits)));
}
