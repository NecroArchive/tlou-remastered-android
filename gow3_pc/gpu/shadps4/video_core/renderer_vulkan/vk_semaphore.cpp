// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <limits>
#include <cstdio>
#include <chrono>
#include <cstdlib>
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_semaphore.h"

#include "common/assert.h"

namespace Vulkan {
void DumpGpuCheckpoints(const Instance& instance);
void DumpDeviceFault(const Instance& instance);

constexpr u64 WAIT_TIMEOUT = 1000000000;
namespace {
// Publish the observed driver counter monotonically. Concurrent refreshers
// cannot move completion backwards or require another driver query.
void PublishCompleted(std::atomic<u64>& known, u64 completed) {
    u64 old = known.load(std::memory_order_acquire);
    while (old < completed &&
           !known.compare_exchange_weak(old, completed, std::memory_order_release,
                                        std::memory_order_relaxed)) {}
}
#ifdef __ANDROID__
struct TimelineStats {
    u64 queries{}, waits{}, query_ns{}, wait_ns{}, avoided{};
    std::chrono::steady_clock::time_point last = std::chrono::steady_clock::now();
    void Report() {
        if (((queries + waits + avoided) & 255u) != 0 ||
            std::chrono::steady_clock::now() - last < std::chrono::seconds(5)) return;
        std::printf("TLOU timeline driver (5 s, per-thread): queries=%llu query-ms=%.2f waits=%llu wait-ms=%.2f avoided=%llu\n",
                    (unsigned long long)queries, query_ns / 1e6,
                    (unsigned long long)waits, wait_ns / 1e6,
                    (unsigned long long)avoided);
        queries = waits = query_ns = wait_ns = avoided = 0;
        last = std::chrono::steady_clock::now();
    }
};
thread_local TimelineStats timeline_stats;
bool TimelineProfile() {
    static const bool enabled = [] {const char* e=std::getenv("TLOU_DRIVER_WAIT_PROFILE");return e && e[0]=='1';}();
    return enabled;
}
#endif
}


Semaphore::Semaphore(const Instance& instance_) : instance{instance_} {
    const vk::StructureChain semaphore_chain = {
        vk::SemaphoreCreateInfo{},
        vk::SemaphoreTypeCreateInfo{
            .semaphoreType = vk::SemaphoreType::eTimeline,
            .initialValue = 0,
        },
    };
    auto [semaphore_result, sem] =
        instance.GetDevice().createSemaphoreUnique(semaphore_chain.get());
    ASSERT_MSG(semaphore_result == vk::Result::eSuccess, "Failed to create master semaphore: {}",
               vk::to_string(semaphore_result));
    semaphore = std::move(sem);
}

Semaphore::~Semaphore() = default;

void Semaphore::NoteAvoidedRefresh() noexcept {
#ifdef __ANDROID__
    if (TimelineProfile()) {
        ++timeline_stats.avoided;
        timeline_stats.Report();
    }
#endif
}

void Semaphore::Refresh() {
#ifdef __ANDROID__
    const bool profile = TimelineProfile();
    const auto start = profile ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
#endif
    auto [counter_result, counter] = instance.GetDevice().getSemaphoreCounterValue(*semaphore);
#ifdef __ANDROID__
    if (profile) {
        ++timeline_stats.queries;
        timeline_stats.query_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - start).count();
        timeline_stats.Report();
    }
#endif
    if (counter_result == vk::Result::eErrorDeviceLost) {
        DumpGpuCheckpoints(instance);
        DumpDeviceFault(instance);
    }
    ASSERT_MSG(counter_result == vk::Result::eSuccess,
               "Failed to get master semaphore value: {}", vk::to_string(counter_result));
    // Concurrent refreshers may already have published a newer value.
    // CAS the maximum of the snapshots; never call the driver again merely
    // because another CPU thread won the atomic race.
    PublishCompleted(gpu_tick, counter);
}

bool Semaphore::WaitFor(u64 tick, u64 timeout_ns) {
    if (IsFree(tick)) {
        return true;
    }
    const vk::SemaphoreWaitInfo wait_info = {
        .semaphoreCount = 1,
        .pSemaphores = &semaphore.get(),
        .pValues = &tick,
    };
#ifdef __ANDROID__
    const bool profile = TimelineProfile();
    const auto start = profile ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
#endif
    const auto result = instance.GetDevice().waitSemaphores(&wait_info, timeout_ns);
#ifdef __ANDROID__
    if (profile) {
        ++timeline_stats.waits;
        timeline_stats.wait_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - start).count();
        timeline_stats.Report();
    }
#endif
    if (result != vk::Result::eSuccess) {
        return false; // timeout (a lost device shows in the next plain Wait)
    }
    // A successful timeline wait itself proves completion of the requested
    // value. Publish only that lower bound; never speculate beyond it.
    PublishCompleted(gpu_tick, tick);
    NoteAvoidedRefresh();
    return true;
}

void Semaphore::Wait(u64 tick) {
    // No need to wait if the GPU is ahead of the tick
    if (IsFree(tick)) {
        return;
    }
    // Update the GPU tick and try again
    Refresh();
    if (IsFree(tick)) {
        return;
    }

    // If none of the above is hit, fallback to a regular wait
    const vk::SemaphoreWaitInfo wait_info = {
        .semaphoreCount = 1,
        .pSemaphores = &semaphore.get(),
        .pValues = &tick,
    };

    unsigned seconds=0;
    for (;;) {
#ifdef __ANDROID__
        const bool profile = TimelineProfile();
        const auto start = profile ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
#endif
        const auto result=instance.GetDevice().waitSemaphores(&wait_info, WAIT_TIMEOUT);
#ifdef __ANDROID__
        if (profile) {
            ++timeline_stats.waits;
            timeline_stats.wait_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - start).count();
            timeline_stats.Report();
        }
#endif
        if (result==vk::Result::eSuccess) break;
        if (result==vk::Result::eErrorDeviceLost) {
            DumpGpuCheckpoints(instance);
            DumpDeviceFault(instance);
        }
        ASSERT_MSG(result==vk::Result::eTimeout, "GPU semaphore wait failed: {}",vk::to_string(result));
        if (++seconds==1 || seconds%10==0) {
            DumpGpuCheckpoints(instance);
            Refresh();
            std::fprintf(stderr,"GPU wait: %u seconds, target=%llu completed=%llu submitted-next=%llu\n",seconds,
                         (unsigned long long)tick,(unsigned long long)KnownGpuTick(),
                         (unsigned long long)CurrentTick());
        }
    }
    // A successful timeline wait itself proves completion of the requested
    // value. Publish only that lower bound; never speculate beyond it.
    PublishCompleted(gpu_tick, tick);
    NoteAvoidedRefresh();
}

} // namespace Vulkan
