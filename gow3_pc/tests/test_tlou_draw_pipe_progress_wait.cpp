// SPDX-License-Identifier: GPL-2.0-or-later
// One producer, one draw-record consumer: stress lost wakes, exact
// completion fences and backpressure with the production atomic protocol.
#include "video_core/renderer_vulkan/tlou_draw_pipe_progress_wait.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <utility>

using namespace Vulkan::Detail;
using u64 = std::uint64_t;

int main() {
    std::atomic<u64> done{0};
    std::atomic<bool> waiter{false};
    u64 parks = 0, parked_ns = 0;
    constexpr u64 Iterations = 40000;

    // Consumer publishes one packet completion at a time, including periods
    // where the producer first registers a wait or wakes late.
    std::jthread consumer([&] {
        for (u64 i = 1; i <= Iterations; ++i) {
            if ((i & 127) == 0) std::this_thread::yield();
            PublishPipeProgress(done, waiter, i);
        }
    });
    for (u64 target = 1; target <= Iterations; ++target) {
        WaitForPipeProgress(done, waiter,
            [target](u64 value) { return value >= target; },
            [] { std::this_thread::yield(); }, parks, parked_ns, 0);
        assert(done.load(std::memory_order_acquire) >= target);
    }
    consumer.join();
    assert(done.load() == Iterations);
    assert(!waiter.load());

    // A producer blocked by limited packet-ring capacity must not overwrite
    // an unconsumed packet. Progress wakes on every completed packet.
    done.store(0);
    constexpr u64 Capacity = 8;
    std::jthread stage_b([&] {
        for (u64 i = 1; i <= 2000; ++i) {
            if ((i & 15) == 0) std::this_thread::yield();
            PublishPipeProgress(done, waiter, i);
        }
    });
    for (u64 at = 1; at <= 2000; ++at) {
        WaitForPipeProgress(done, waiter,
            [at](u64 completed) { return at <= completed + Capacity; },
            [] { std::this_thread::yield(); }, parks, parked_ns, 0);
        assert(at <= done.load(std::memory_order_acquire) + Capacity);
    }
    stage_b.join();
    assert(!waiter.load());
    std::printf("TLOU pipe producer waits: 40000 packet fences + "
                "2000 capacity checks PASS (parks=%llu)\n",
                static_cast<unsigned long long>(parks));
}
