// SPDX-License-Identifier: GPL-2.0-or-later
// gow3: two-stage draw pipeline (docs/parallel_gpu.md, "Two-stage draw pipeline").
//
// The GPU command thread (stage A) decodes PM4, keeps the register file and selects pipelines;
// for a direct draw it writes a packet (the register blocks changed since the previous packet,
// the stages' user data, the draw parameters) into this ring and goes on decoding. The draw
// recording thread (stage B) owns the caches, the barriers and the command recording while
// packets are in flight: it applies each packet to its own copy of the registers and runs the
// rest of the draw. Anything else stage A does on that state waits for stage B to run dry
// (Drain) and then runs on stage A as before.

#pragma once

#include <cstdlib>
#include "gow3_threads.h"
#include "gow3_toggles.h"
#include "gow3_profiler.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <thread>
#include "common/arch.h"
#include "common/rdtsc.h"
#include "video_core/renderer_vulkan/vk_draw_pipe_busy_sample.h"
#include "video_core/renderer_vulkan/tlou_draw_pipe_progress_wait.h"
#include "video_core/renderer_vulkan/tlou_adaptive_pipe_drain.h"

#include "common/assert.h"
#include "common/thread.h"
#include "common/types.h"

namespace Vulkan {

class DrawPipe {
public:
    /// Runs a packet on stage B.
    using Handler = void (*)(void* context, const u8* packet, u32 size);

    DrawPipe(Handler handler_, void* context_) : handler{handler_}, context{context_} {
        ring = std::make_unique<u8[]>(Capacity);
        thread = std::jthread([this](std::stop_token stop) { Run(stop); });
    }

    ~DrawPipe() {
        Drain();
        thread.request_stop();
        published.fetch_add(0, std::memory_order_seq_cst);
        wake.fetch_add(1, std::memory_order_seq_cst);
        wake.notify_one();
    }

    /// True on the draw recording thread.
    [[nodiscard]] static bool OnStageB() noexcept {
        return on_stage_b;
    }

    [[nodiscard]] u32 StageBThreadId() const noexcept {
        return stage_b_tid.load(std::memory_order_acquire);
    }

    /// Stage A: space for a packet of `size` bytes (8-byte aligned), valid until Commit().
    u8* Begin(u32 size) {
        size = Align(size + sizeof(Header));
        ASSERT(size <= Capacity / 4);
        u64 at = head;
        const u64 offset = at % Capacity;
        if (offset + size > Capacity) {
            // Wrap: the rest of the ring is skipped (a header with zero size marks it).
            WaitForSpace(at, Capacity - offset);
            reinterpret_cast<Header*>(ring.get() + offset)->size = 0;
            at += Capacity - offset;
            head = at;
            published.store(at, std::memory_order_release);
        }
        WaitForSpace(at, size);
        pending_size = size;
        auto* header = reinterpret_cast<Header*>(ring.get() + at % Capacity);
        header->size = size;
        return reinterpret_cast<u8*>(header + 1);
    }

    /// Stage A: hands the packet from Begin() to stage B.
    void Commit(u32 payload_size) {
        reinterpret_cast<Header*>(ring.get() + head % Capacity)->payload = payload_size;
        head += pending_size;
        published.store(head, std::memory_order_seq_cst);
        ++packets;
        if (sleeping.load(std::memory_order_seq_cst)) {
            wake.fetch_add(1, std::memory_order_seq_cst);
            wake.notify_one();
        }
    }

    /// Why stage A drained (statistics): a PM4 opcode, or one of these.
    enum Reason : u32 {
        ReasonConstRam = 256,
        ReasonCommands,
        ReasonCompute,
        ReasonSubmissionEnd,
        ReasonDraw,
        ReasonRasterizer,
        NumReasons,
    };
    std::array<u64, NumReasons> drains_by_reason{};
    std::array<u64, NumReasons> cycles_by_reason{};

    /// Stage A: waits until stage B has run every committed packet.
    void Drain(u32 reason = ReasonRasterizer) {
        if (consumed.load(std::memory_order_acquire) == head) {
            return;
        }
        ++drains;
        ++sync_stats.drain_waits;
        const u32 slot = reason < NumReasons ? reason : ReasonRasterizer;
        ++drains_by_reason[slot];
        // The Android release does not enable GOW3_FRAME_STATS. Two serializing
        // counter reads on every waiting drain add CPU cost without measuring
        // useful emulator work. Preserve exact timings when explicitly profiling.
        static const bool profile_drains = std::getenv("GOW3_FRAME_STATS") != nullptr;
        const u64 start = profile_drains ? Common::FencedRDTSC() : 0;
#ifdef __ANDROID__
        if (ParkProducerEnabled()) {
            const u64 target = head;
            Detail::AdaptivePipeDrain(
                [this, target] { return consumed.load(std::memory_order_acquire) >= target; },
                [] { COMMON_CPU_RELAX(); }, [] { std::this_thread::yield(); },
                [this, target] {
                    Detail::WaitForPipeTargetProgress(
                        consumed, target_signal, target,
                        [] { COMMON_CPU_RELAX(); },
                        sync_stats.parks, sync_stats.parked_ns, 0);
                });
        } else
#endif
        {
            for (u32 spins = 0; consumed.load(std::memory_order_acquire) != head; ++spins) {
                if (spins < 4096) COMMON_CPU_RELAX();
                else std::this_thread::yield();
            }
        }
        if (profile_drains) {
            const u64 waited = Common::FencedRDTSC() - start;
            drain_cycles += waited;
            cycles_by_reason[slot] += waited;
        }
    }

    // A pending GPU-write dependency also waits for Stage B to reach its
    // precise packet position; avoid burning a CPU core in this dependency.
    void WaitUntil(u64 target) {
        if (consumed.load(std::memory_order_acquire) >= target) return;
        ++sync_stats.dependency_waits;
#ifdef __ANDROID__
        if (ParkProducerEnabled()) {
            Detail::WaitForPipeTargetProgress(
                consumed, target_signal, target,
                [] { COMMON_CPU_RELAX(); }, sync_stats.parks, sync_stats.parked_ns);
            return;
        }
#endif
        for (u32 spins = 0; consumed.load(std::memory_order_acquire) < target; ++spins) {
            if (spins < 4096) COMMON_CPU_RELAX();
            else std::this_thread::yield();
        }
    }

    struct SyncStats {
        u64 drain_waits = 0;
        u64 capacity_waits = 0;
        u64 dependency_waits = 0;
        u64 parks = 0;
        u64 parked_ns = 0;
        u64 notifications = 0;
    };
    // Stage A is the sole writer and reader of these per-window counters.
    [[nodiscard]] SyncStats TakeSyncStats() {
        auto values = sync_stats;
#ifdef __ANDROID__
        const u64 epoch = target_signal.epoch.load(std::memory_order_relaxed);
        values.notifications = epoch - last_reported_notification_epoch;
        last_reported_notification_epoch = epoch;
#endif
        sync_stats = {};
        return values;
    }

    /// Stage A: position after the last committed packet; Reached(position) once B ran it.
    [[nodiscard]] u64 Head() const noexcept {
        return head;
    }
    [[nodiscard]] bool Reached(u64 position) const noexcept {
        return consumed.load(std::memory_order_acquire) >= position;
    }

    /// Stage A: one acquire snapshot for several pending-write checks. Stage B
    /// only advances consumed, so checking against a snapshot can conservatively
    /// retain a completed write, but can never retire an unfinished one.
    [[nodiscard]] u64 CompletedPosition() const noexcept {
        return consumed.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool Idle() const noexcept {
        return consumed.load(std::memory_order_acquire) == head;
    }

    /// Statistics (stage A): packets, drains that had to wait, cycles waited, cycles stage B
    /// spent running packets.
    u64 packets = 0, drains = 0, drain_cycles = 0;
    std::atomic<u64> busy_cycles{0};

private:
    static bool ParkProducerEnabled() {
#ifdef __ANDROID__
        static const bool enabled = [] {
            const char* env = std::getenv("GOW3_TLOU_PIPE_PARK");
            return !(env && env[0] == '0' && env[1] == '\0');
        }();
        return enabled;
#else
        return false;
#endif
    }

    struct Header {
        u32 size;    ///< bytes to the next packet (0: wrap to the ring start)
        u32 payload; ///< bytes written by the producer
    };
    static constexpr u64 Capacity = 16ull << 20;

    static constexpr u32 Align(u32 size) {
        return (size + 63) & ~63u;
    }

    void WaitForSpace(u64 at, u64 size) {
        if (at + size - consumed.load(std::memory_order_acquire) <= Capacity) return;
        ++sync_stats.capacity_waits;
#ifdef __ANDROID__
        if (ParkProducerEnabled()) {
            // Capacity only needs Stage B to consume the overwritten prefix.
            const u64 target = at + size - Capacity;
            Detail::WaitForPipeTargetProgress(
                consumed, target_signal, target,
                [] { COMMON_CPU_RELAX(); }, sync_stats.parks, sync_stats.parked_ns);
            return;
        }
#endif
        while (at + size - consumed.load(std::memory_order_acquire) > Capacity) {
            COMMON_CPU_RELAX();
        }
    }

    void Run(std::stop_token stop) {
        Common::SetCurrentThreadName("gow3:DrawRec");
#ifdef __ANDROID__
        tlou_cpu_policy_apply("draw recording",1);
#endif
#ifndef _WIN32
        if (clockid_t clock; pthread_getcpuclockid(pthread_self(), &clock) == 0)
            Gow3Stats::draw_thread_clock.store(static_cast<int>(clock));
#endif
#ifdef _WIN32
        Gow3Profiler::Start(GetCurrentThreadId(), "draw recording thread");
        Gow3Threads::ApplyGpuThreadPriority("draw recording thread");
#endif
        on_stage_b = true;
        stage_b_tid.store(static_cast<u32>(gettid()), std::memory_order_release);
        // The optional pipe report is the only reader of busy_cycles. Cache its
        // status once instead of serializing two CPU-counter reads per packet.
        const char* trim_stats=std::getenv("TLOU_DRAW_COUNTER_TRIM");
        const bool sample_busy = std::getenv("GOW3_FRAME_STATS") != nullptr ||
            !(trim_stats && trim_stats[0]=='1' && trim_stats[1]==0);
        u64 at = 0;
        while (true) {
            // Packets follow each other within microseconds while a frame is decoded: spin,
            // and sleep only after a longer pause (between frames).
            u64 available = published.load(std::memory_order_acquire);
            if (available == at) {
                // With few hardware threads (Steam Deck: 8) a long spin takes time from the
                // game's own threads. GOW3_PIPE_SPIN_US overrides.
                static const auto spin_time = std::chrono::microseconds([] {
                    if (const char* env = std::getenv("GOW3_PIPE_SPIN_US")) {
                        return std::max(0, std::atoi(env));
                    }
                    return Gow3Threads::Available() >= 12 ? 200 : 50;
                }());
                const auto spin_until = std::chrono::steady_clock::now() + spin_time;
                for (u32 spins = 1; available == at; ++spins) {
                    if (stop.stop_requested()) {
                        return;
                    }
                    COMMON_CPU_RELAX();
                    if (!(spins & 255) && std::chrono::steady_clock::now() >= spin_until) {
                        const u32 seen = wake.load(std::memory_order_seq_cst);
                        sleeping.store(true, std::memory_order_seq_cst);
                        if (published.load(std::memory_order_seq_cst) == at &&
                            !stop.stop_requested()) {
                            wake.wait(seen, std::memory_order_seq_cst);
                        }
                        sleeping.store(false, std::memory_order_relaxed);
                    }
                    available = published.load(std::memory_order_acquire);
                }
            }
            const auto* header = reinterpret_cast<const Header*>(ring.get() + at % Capacity);
            if (header->size == 0) {
                at += Capacity - at % Capacity;
#ifdef __ANDROID__
                if (ParkProducerEnabled()) Detail::PublishPipeTargetProgress(consumed, target_signal, at);
                else consumed.store(at, std::memory_order_release);
#else
                consumed.store(at, std::memory_order_release);
#endif
                continue;
            }
            const u64 start = Detail::BeginDrawPipeBusySample(sample_busy);
            // gow3 21 Oct 2026, TEST KNOB (default off): GOW3_TEST_PIPE_DELAY_NS=<n> makes stage B
            // busy-wait n ns before each packet, like a slower or loaded CPU (laptops). Used to
            // reproduce the multithreaded late-constants race: stage A walks a draw's tables
            // while the scalar copy before it still waits here (GOW3_SRT_PENDING_DRAIN).
            static const u64 test_delay_ns = [] {
                const char* env = std::getenv("GOW3_TEST_PIPE_DELAY_NS");
                return env ? std::strtoull(env, nullptr, 10) : 0ull;
            }();
            if (test_delay_ns) {
                const auto until = std::chrono::steady_clock::now() +
                                   std::chrono::nanoseconds(test_delay_ns);
                while (std::chrono::steady_clock::now() < until) {
                    COMMON_CPU_RELAX();
                }
            }
            handler(context, reinterpret_cast<const u8*>(header + 1), header->payload);
            Detail::EndDrawPipeBusySample(sample_busy, start, busy_cycles);
            at += header->size;
#ifdef __ANDROID__
            if (ParkProducerEnabled()) Detail::PublishPipeTargetProgress(consumed, target_signal, at);
            else consumed.store(at, std::memory_order_release);
#else
            consumed.store(at, std::memory_order_release);
#endif
        }
    }

    static inline thread_local bool on_stage_b = false;
    Handler handler;
    void* context;
    std::unique_ptr<u8[]> ring;
    u64 head = 0; ///< stage A's write position
    u32 pending_size = 0;
    alignas(64) std::atomic<u64> published{0};
    alignas(64) std::atomic<u64> consumed{0};
    // Sequenced with PublishPipeProgress on Stage B, so no missed wakeups.
    Detail::PipeTargetSignal target_signal{};
    u64 last_reported_notification_epoch = 0; // Stage A only
    SyncStats sync_stats{};
    alignas(64) std::atomic<u32> wake{0};
    std::atomic<bool> sleeping{false};
    std::atomic<u32> stage_b_tid{0};
    std::jthread thread;
};

} // namespace Vulkan
