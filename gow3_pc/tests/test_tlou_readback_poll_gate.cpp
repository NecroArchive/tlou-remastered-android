// SPDX-License-Identifier: GPL-2.0-or-later
#include "../gpu/shadps4/video_core/buffer_cache/tlou_readback_poll_gate.h"
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <deque>
#include <random>
#include <vector>
#include <string_view>
using u64=std::uint64_t;
struct Readback { u64 tick; bool watch=false; };
struct State {
    u64 current=1, completed=0, ticks_polled=0, refreshes=0, watch_sets=0, landings=0;
    std::deque<Readback> queue;
    void Complete() {
        for(auto& r:queue)
            if(!r.watch && r.tick<current){r.watch=true;++watch_sets;}
        while(!queue.empty()){
            auto& r=queue.front();
            if(r.tick>completed){
                if((++ticks_polled & 15)==0) ++refreshes;
                return;
            }
            ++landings;
            queue.pop_front();
        }
    }
    void PollOld() { if(!queue.empty())Complete(); }
    void PollNew() {
        if(!queue.empty() &&
            !VideoCore::Detail::AllReadbacksUnsubmitted(queue.front().tick,current))
            Complete();
    }
};
static void Parity() {
    assert(VideoCore::Detail::AllReadbacksUnsubmitted(1,1));
    assert(!VideoCore::Detail::AllReadbacksUnsubmitted(1,2));
    for(u64 seed=1;seed<1000;++seed){
        State old,now;
        std::mt19937_64 rng(seed);
        for(int i=0;i<6000;++i){
            const auto op=rng()%12;
            if(op<3){
                // Enqueue readbacks against the current unsubmitted tick.
                const auto t=old.current;
                old.queue.push_back({t});now.queue.push_back({t});
            }else if(op==3){
                // Submitting a buffer advances the scheduler next tick.
                ++old.current;++now.current;
            }else if(op==4 && old.completed+1<old.current){
                ++old.completed;++now.completed;
            }else{
                old.PollOld();now.PollNew();
            }
            // Watch installation and landing must match. The two implementations
            // can differ in *unsuccessful* polling counter only.
            assert(old.current==now.current && old.completed==now.completed);
            assert(old.queue.size()==now.queue.size());
            for(size_t j=0;j<old.queue.size();++j){
                assert(old.queue[j].tick==now.queue[j].tick);
                assert(old.queue[j].watch==now.queue[j].watch);
            }
            assert(old.watch_sets==now.watch_sets && old.landings==now.landings);
        }
    }
}
static volatile u64 sink=0;
static double Timed(bool fast,int case_id){
    State s;
    s.current=50;s.completed=48;
    s.queue.push_back({case_id==0?50u:49u});
    s.queue.push_back({50u});
    auto before=std::chrono::steady_clock::now();
    for(unsigned rep=0;rep<1800000;++rep){
        // Hold this readback in the queue, as if a GPU command buffer is
        // unsent (case 0) or submitted but not completed (case 1).
        if(fast)s.PollNew();else s.PollOld();
    }
    auto end=std::chrono::steady_clock::now();
    sink=s.ticks_polled+s.refreshes;
    return std::chrono::duration<double,std::nano>(end-before).count()/1800000.;
}
int main(int argc,char**argv){
    Parity();
    std::puts("readback FIFO tick gate: 1000 randomized timelines exact parity PASS");
    if(argc>1 && std::string_view(argv[1])=="--correctness-only")return 0;
    for(int scenario=0;scenario<2;++scenario){
        double a=0,b=0;
        for(int rep=0;rep<5;++rep){
            if(rep&1){b+=Timed(true,scenario);a+=Timed(false,scenario);}
            else {a+=Timed(false,scenario);b+=Timed(true,scenario);}
        }
        a/=5;b/=5;
        std::printf("scenario=%s old=%.2f ns/poll gated=%.2f ns/poll speedup=%.2fx gain=%.2f ns/poll\n",
            scenario==0?"unsubmitted":"submitted",a,b,a/b,a-b);
    }
}
