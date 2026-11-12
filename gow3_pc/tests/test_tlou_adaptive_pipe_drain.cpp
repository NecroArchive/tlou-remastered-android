// SPDX-License-Identifier: GPL-2.0-or-later
// Real production adaptive spin/yield/park protocol; race, ordering & latency.
#include "video_core/renderer_vulkan/tlou_adaptive_pipe_drain.h"
#include "video_core/renderer_vulkan/tlou_draw_pipe_progress_wait.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>
using namespace std::chrono;
using u64=std::uint64_t;
struct Trial {u64 parks=0,parked_ns=0,elapsed_us=0;};

Trial Run(int work_us,int n) {
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
        Vulkan::Detail::AdaptivePipeDrain(
            [&]{return done.load(std::memory_order_acquire)>=u64(i);},
            []{std::atomic_signal_fence(std::memory_order_seq_cst);},
            []{std::this_thread::yield();},
            [&]{Vulkan::Detail::WaitForPipeProgress(
                done,producer_waiting,
                [target=u64(i)](u64 v){return v>=target;},
                []{std::atomic_signal_fence(std::memory_order_seq_cst);},
                t.parks,t.parked_ns,0);});
        assert(done.load(std::memory_order_acquire)>=u64(i));
    }
    worker.join();
    t.elapsed_us=duration_cast<microseconds>(steady_clock::now()-begin).count();
    assert(!producer_waiting.load());
    return t;
}

int main() {
    auto fast=Run(15,5000);
    auto slow=Run(2000,100);
    // Every completion is acknowledged precisely, including the futex
    // transition when Stage B reports progress during waiter registration.
    assert(slow.parks>0);
    std::printf("adaptive draw-pipe 15us: %llu parks/%d, %.2f us/op; "
                "2ms: %llu parks/%d, %.2f us/op PASS (host, not FPS)\n",
                (unsigned long long)fast.parks,5000,double(fast.elapsed_us)/5000,
                (unsigned long long)slow.parks,100,double(slow.elapsed_us)/100);
}
