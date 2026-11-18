// SPDX-License-Identifier: GPL-2.0-or-later
// Exact production Stage-A / Stage-B target-aware notification protocol.
// Verifies pending IB dependency, wake registration race, capacity threshold
// and repeated packet completion, then A/B compares CPU publish overhead.
// This is a host test, not an Android FPS benchmark.
#include "video_core/renderer_vulkan/tlou_draw_pipe_progress_wait.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <utility>
#include <string_view>

using namespace Vulkan::Detail;
using u64 = std::uint64_t;

void ExactTargetAndRegistrationRace() {
    constexpr u64 packets = 4096;
    for (unsigned round = 0; round < 200; ++round) {
        std::atomic<u64> done{0};
        PipeTargetSignal signal;
        u64 parks = 0, parked_ns = 0;
        std::jthread consumer([&] {
            while (signal.target.load(std::memory_order_seq_cst) ==
                   PipeTargetSignal::NoTarget) std::this_thread::yield();
            for (u64 i = 1; i <= packets; ++i) {
                PublishPipeTargetProgress(done, signal, i);
                if ((i & 63u) == 0) std::this_thread::yield();
            }
        });
        WaitForPipeTargetProgress(done, signal, packets,
                                  [] { std::this_thread::yield(); },
                                  parks, parked_ns, 0);
        consumer.join();
        assert(done.load() == packets);
        assert(signal.target.load() == PipeTargetSignal::NoTarget);
        // One notification at the exact target, not 4096 premature ones.
        assert(signal.epoch.load() == 1);
    }
}

void MovingTargets() {
    std::atomic<u64> done{0};
    PipeTargetSignal signal;
    u64 parks = 0, parked_ns = 0;
    constexpr u64 packets = 80000;
    std::jthread consumer([&] {
        for (u64 i=1; i<=packets; ++i) {
            PublishPipeTargetProgress(done, signal, i);
            if ((i & 127) == 0) std::this_thread::yield();
        }
    });
    for (u64 target = 1; target <= packets; target += 37) {
        WaitForPipeTargetProgress(done, signal, target,
                                  [] { std::this_thread::yield(); },
                                  parks, parked_ns, 0);
        assert(done.load(std::memory_order_acquire) >= target);
    }
    consumer.join();
    assert(done.load() == packets);
    assert(signal.target.load() == PipeTargetSignal::NoTarget);
    assert(signal.epoch.load() <= packets);
}

void CapacityPressure() {
    std::atomic<u64> done{0};
    PipeTargetSignal signal;
    u64 parks = 0, parked_ns = 0;
    constexpr u64 capacity=128, packets=20000;
    std::jthread consumer([&] {
        for (u64 i=1;i<=packets;++i) {
            PublishPipeTargetProgress(done, signal, i);
            if ((i & 255) == 0) std::this_thread::yield();
        }
    });
    for (u64 at=capacity+1;at<=packets;at+=7) {
        WaitForPipeTargetProgress(done, signal, at-capacity,
                                  [] { std::this_thread::yield(); },
                                  parks, parked_ns, 0);
        assert(done.load(std::memory_order_acquire)+capacity>=at);
    }
    consumer.join();
}

std::pair<double,double> ComparePublishCost() {
    // Real production store/notification code, 100000 progress packets.
    // Old Stage A waiter covers all 100000 positions, so old Stage B wakes
    // it every time; new Stage B wakes exactly once at its target.
    constexpr u64 packets=100000;
    std::array<double,7> old_cost{},new_cost{};
    for(int run=0;run<7;++run) {
        for(int pair=0;pair<2;++pair) {
            const bool newer=((run+pair)&1)!=0;
            std::atomic<u64> done{0};
            std::atomic<bool> old_waiting{true};
            PipeTargetSignal signal;
            signal.target.store(packets,std::memory_order_seq_cst);
            const auto begin=std::chrono::steady_clock::now();
            for(u64 i=1;i<=packets;++i) {
                if(newer) PublishPipeTargetProgress(done,signal,i);
                else PublishPipeProgress(done,old_waiting,i);
            }
            const auto us=std::chrono::duration<double,std::micro>(
                std::chrono::steady_clock::now()-begin).count();
            (newer?new_cost:old_cost)[run]=us;
            assert(done.load()==packets);
            if(newer) assert(signal.epoch.load()==1);
        }
    }
    std::sort(old_cost.begin(),old_cost.end());
    std::sort(new_cost.begin(),new_cost.end());
    return {old_cost[3],new_cost[3]};
}

int main(int argc,char**argv) {
    ExactTargetAndRegistrationRace();
    MovingTargets();
    CapacityPressure();
    std::printf("target-only pipe: 200x4096 registration races, 80000 "
                "moving targets and 20000 capacity checks PASS\n");
    if(argc==2 && std::string_view(argv[1])=="--correctness-only") return 0;
    const auto [old_us,new_us]=ComparePublishCost();
    std::printf("100000 Stage-B packet completions: old=%.1fus, new=%.1fus "
                "(%.1f%% less CPU), 100000 old notifications vs 1 new "
                "(host test, NOT game FPS)\n",
                old_us,new_us,100.0*(old_us-new_us)/old_us);
    if(new_us > old_us*0.9) {
        std::fprintf(stderr,"Targeted completion did not pass 10%% CPU cost gate\n");
        return 2;
    }
}
