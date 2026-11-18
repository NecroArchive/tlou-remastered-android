// SPDX-License-Identifier: GPL-2.0-or-later
// Production helper correctness: fence completion must not wait for newer
// unrelated tasks, while a worker-less configuration must still make progress.
#include "../gpu/shadps4/video_core/renderer_vulkan/tlou_deferred_signal_wait.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <thread>

using Vulkan::Detail::WaitForDeferredSignal;
using namespace std::chrono_literals;

int main() {
    {
        std::atomic<std::uint64_t> completed{0};
        std::atomic<bool> allow_later_job{false};
        std::atomic<bool> later_job_finished{false};
        std::thread pool([&] {
            std::this_thread::sleep_for(3ms);
            completed.store(1, std::memory_order_release);
            completed.notify_all();
            // Represents newer copy work: the requested fence is already
            // signalled even though the pool cannot be fully idle.
            while (!allow_later_job.load(std::memory_order_acquire))
                std::this_thread::yield();
            later_job_finished.store(true, std::memory_order_release);
        });
        int unwanted_help=0;
        WaitForDeferredSignal(completed, 1, true, [&] { ++unwanted_help; });
        assert(unwanted_help==0);
        assert(completed.load(std::memory_order_acquire)==1);
        assert(!later_job_finished.load(std::memory_order_acquire));
        allow_later_job.store(true, std::memory_order_release);
        pool.join();
        assert(later_job_finished.load(std::memory_order_acquire));
    }
    {
        // Do not wait only for ANY advancement: must wait for target epoch.
        std::atomic<std::uint64_t> completed{0};
        std::thread producer([&] {
            for (std::uint64_t n=1;n<=4;n++) {
                std::this_thread::sleep_for(1ms);
                completed.store(n, std::memory_order_release);
                completed.notify_all();
            }
        });
        WaitForDeferredSignal(completed, 4, true, [] { assert(false); });
        assert(completed.load(std::memory_order_acquire)==4);
        producer.join();
    }
    {
        // When ParallelCopies is disabled, WaitAsync must keep executing
        // queued work; an atomic sleep would deadlock on this configuration.
        std::atomic<std::uint64_t> completed{0};
        unsigned assists=0;
        WaitForDeferredSignal(completed, 2, false, [&] {
            ++assists;
            completed.fetch_add(1, std::memory_order_release);
            completed.notify_all();
        });
        assert(assists==2);
    }
}
