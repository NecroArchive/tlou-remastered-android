// SPDX-License-Identifier: GPL-2.0-or-later
// Real production adaptive spin/yield/park protocol; race, ordering & latency.
#include "video_core/renderer_vulkan/tlou_adaptive_pipe_drain.h"
#include "video_core/renderer_vulkan/tlou_draw_pipe_progress_wait.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <thread>
using namespace std::chrono;
using u64=std::uint64_t;
struct Trial {u64 parks=0,parked_ns=0,elapsed_us=0;};

Trial Run(int work_us,int n,bool legacy_park=false) {
    std::atomic<u64> published{0},done{0};
    std::atomic<bool> producer_waiting{false};
    Trial t;
    std::jthread worker([&]{
        for(int i=1;i<=n;++i) {
            while(published.load(std::memory_order_acquire)<u64(i)) std::this_thread::yield();
            const auto deadline=steady_clock::now()+microseconds{work_us};
            while(steady_clock::now()<deadline) std::atomic_signal_fence(std::memory_order_seq_cst);
            Vulkan::Detail::PublishPipeProgress(done,producer_waiting,u64(i));
        }
    });
    const auto begin=steady_clock::now();
    for(int i=1;i<=n;++i) {
        published.store(u64(i),std::memory_order_release);
        const auto ready=[&]{return done.load(std::memory_order_acquire)>=u64(i);};
        const auto relax=[]{std::atomic_signal_fence(std::memory_order_seq_cst);};
        if (legacy_park) {
            // Exact pre-fix Drain() protocol: 4096 relax iterations,
            // then atomic blocking and notification on every short wait.
            Vulkan::Detail::WaitForPipeProgress(
                done,producer_waiting,
                [target=u64(i)](u64 v){return v>=target;},
                relax,t.parks,t.parked_ns);
        } else {
            Vulkan::Detail::AdaptivePipeDrain(
                ready,relax,[]{std::this_thread::yield();},
                [&]{Vulkan::Detail::WaitForPipeProgress(
                    done,producer_waiting,
                    [target=u64(i)](u64 v){return v>=target;},
                    relax,t.parks,t.parked_ns,0);});
        }
        assert(done.load(std::memory_order_acquire)>=u64(i));
    }
    worker.join();
    t.elapsed_us=duration_cast<microseconds>(steady_clock::now()-begin).count();
    assert(!producer_waiting.load());
    return t;
}

int main(int argc,char** argv) {
    if (argc==2 && std::strcmp(argv[1],"--correctness-only")==0) {
        auto fast=Run(15,1000);
        auto slow=Run(2000,30);
        assert(fast.elapsed_us>0 && slow.parks>0);
        std::printf("TLOU adaptive draw-pipe correctness: short + long waits PASS\n");
        return 0;
    }
    std::array<double,5> legacy{},adaptive{};
    u64 total_legacy_parks=0,total_adaptive_parks=0;
    for(int rep=0;rep<5;++rep) {
        const auto sample=[&](bool old) {
            auto x=Run(15,5000,old);
            (old?total_legacy_parks:total_adaptive_parks)+=x.parks;
            (old?legacy:adaptive)[rep]=double(x.elapsed_us)/5000;
        };
        // Alternating order reduces drift / benchmark warmup bias.
        if(rep&1) {sample(false);sample(true);}
        else {sample(true);sample(false);}
    }
    std::sort(legacy.begin(),legacy.end());
    std::sort(adaptive.begin(),adaptive.end());
    auto slow=Run(2000,100);
    // The long-wait fallback must still sleep safely and wake precisely.
    assert(slow.parks>0);
    const double ratio=adaptive[2]/legacy[2];
    std::printf("draw-pipe handoff 15us (5000 packets x 5 each): "
                "legacy=%.2fus adaptive=%.2fus ratio=%.3f "
                "parks old=%llu new=%llu; 2ms fallback parks=%llu PASS "
                "(host, not FPS)\n",
                legacy[2],adaptive[2],ratio,
                (unsigned long long)total_legacy_parks,
                (unsigned long long)total_adaptive_parks,
                (unsigned long long)slow.parks);
    if (ratio > 0.95) {
        std::fprintf(stderr,"adaptive producer handoff did not clear 5%% "
                            "latency reduction gate on this runner\n");
        return 2;
    }
}
