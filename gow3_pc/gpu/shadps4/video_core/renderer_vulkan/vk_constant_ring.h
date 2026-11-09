// SPDX-License-Identifier: GPL-2.0-or-later
// gow3: small read-only guest buffers (constants) copied by the GPU command thread (stage A of
// the draw pipeline, vk_draw_pipe.h) into this ring, so the draw recording thread only binds
// them. Positions are monotonic byte counts; a region is reused once the submission that
// recorded its last draw has completed on the GPU. Stage B stamps each packet it records with
// the submission tick (Stamp), stage A retires stamps whose tick the GPU has passed.

#pragma once

#include <array>
#include <cstdio>
#include <atomic>

#include "common/assert.h"
#include "common/types.h"
#include "video_core/buffer_cache/buffer.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/tlou_constant_ring_flush.h"
#include "video_core/renderer_vulkan/tlou_constant_ring_stamp.h"
#include "video_core/renderer_vulkan/tlou_constant_ring_recycle_gate.h"

namespace Vulkan {

class ConstantRing {
public:
    static constexpr u64 Capacity = 32ull << 20;

    ConstantRing(const Instance& instance, Scheduler& scheduler_)
        : scheduler{scheduler_},
          buffer{instance, 0, Capacity, VideoCore::MemoryType::Stream, "gow3 constant ring"} {
        ASSERT(!buffer.mapped_data.empty());
        std::printf("GPU: constant ring allocation is %s (batched flush can skip "
                    "per-copy host barriers on coherent memory)\\n",
                    buffer.is_coherent ? "host-coherent" : "non-coherent");
    }

    [[nodiscard]] vk::Buffer Handle() const noexcept {
        return buffer.Handle();
    }

    /// Stage A: `size` bytes at `alignment`; returns the ring offset, or nullopt when the ring is
    /// still in use by the GPU (the caller leaves the binding to stage B).
    std::optional<u64> Allocate(u64 size, u64 alignment) {
        u64 at = (position + alignment - 1) & ~(alignment - 1);
        if (at % Capacity + size > Capacity) {
            at += Capacity - at % Capacity; // wrap to the ring start
        }
        const auto in_use = [&] { return at + size > Capacity && at + size - Capacity > retired; };
        if (in_use()) {
            // Under sustained GPU pressure, the former code called the
            // Vulkan timeline Refresh() on EVERY failed snapshot, sometimes
            // hundreds of thousands of times per five seconds. A ring stamp
            // cannot retire until its published head or known GPU tick moves.
            // Probe immediately on either change and at least once every
            // 32 blocked allocations to discover GPU progress otherwise.
            const auto* semaphore = scheduler.GetWorkSemaphore();
            const u64 head = stamps_head.load(std::memory_order_acquire);
            if (recycle_gate.ShouldProbe(head, semaphore->KnownGpuTick())) {
                ++recycle_probes;
                Retire();
            } else {
                ++recycle_skipped;
            }
            if (in_use()) {
                ++recycle_misses;
                return std::nullopt; // existing correct Stage-B buffer fallback
            }
        }
        position = at + size;
        return at % Capacity;
    }

    [[nodiscard]] u8* Data(u64 offset) noexcept {
        return buffer.mapped_data.data() + offset;
    }
    // Stage-A-only counters; called when the existing 5-second CPU sample
    // emits its report. No additional clock or output in the draw hot path.
    void PrintRecycleStats() {
        const u64 head = stamps_head.load(std::memory_order_acquire);
        const u64 tail = stamps_tail.load(std::memory_order_relaxed);
        std::printf("TLOU constant ring recycle (5 s): used=%.2fMiB "
                    "head=%llu tail=%llu pending-stamps=%llu "
                    "probes=%llu skipped-probes=%llu gpu-refresh=%llu "
                    "empty-probes=%llu stamps-retired=%llu alloc-misses=%llu\n",
                    double(position - retired) / double(1ull << 20),
                    (unsigned long long)head, (unsigned long long)tail,
                    (unsigned long long)(head - tail),
                    (unsigned long long)recycle_probes,
                    (unsigned long long)recycle_skipped,
                    (unsigned long long)recycle_gpu_refreshes,
                    (unsigned long long)recycle_empty_probes,
                    (unsigned long long)recycle_retired_stamps,
                    (unsigned long long)recycle_misses);
        recycle_probes = recycle_skipped = recycle_gpu_refreshes = 0;
        recycle_empty_probes = recycle_retired_stamps = recycle_misses = 0;
    }

    /// The ring's buffer (late-constant refresh, vk_rasterizer.cpp).
    [[nodiscard]] VideoCore::Buffer& Storage() noexcept {
        return buffer;
    }

    /// Stage A, after writing: makes the bytes visible to the GPU.
    void Flush(u64 offset, u64 size) {
        buffer.Flush(offset, size);
    }

    // Stage A owns these monotonic positions. A packet's snapshots cannot
    // reach Stage B until the draw pipe Commit() publishes the packet, so
    // one host-memory flush after collecting all stages suffices. When ring
    // positions wrap, the helper covers both physical spans (no stale data
    // is published to the GPU). Coherent memory remains a no-op.
    void FlushPacket(u64 begin) {
        if (buffer.is_coherent || begin == position) {
            return; // coherent host writes need no Vulkan flush at all
        }
        Detail::ForRingFlushSegments(begin, position, Capacity, [this](u64 offset, u64 size) {
            buffer.Flush(offset, size);
        });
    }

    /// Stage A: the position the next packet's allocations end at.
    [[nodiscard]] u64 Position() const noexcept {
        return position;
    }

    /// Stage B, after recording a packet whose allocations end at `end`. Stamps of one
    /// submission tick are merged: only the last end per tick is published.
    void Stamp(u64 end) {
        // A full ring leaves end unchanged on every failed Allocate(). The
        // previous end-only early return then suppressed tick transitions, so
        // the final stamp was never published and the ring stayed full forever.
        // Check BOTH position and GPU submission epoch before returning.
        stamp_progress.Stamp(end, scheduler.CurrentTick(),
                             [this](u64 prior_end, u64 prior_tick) {
                                 Publish(prior_end, prior_tick);
                             });
    }

private:
    void Publish(u64 end, u64 tick) {
        const u64 at = stamps_head.load(std::memory_order_relaxed);
        if (at - stamps_tail.load(std::memory_order_acquire) >= stamps.size()) {
            return; // full: this region retires with a later stamp
        }
        stamps[at % stamps.size()] = {end, tick};
        stamps_head.store(at + 1, std::memory_order_release);
        stamp_progress.last_published = end;
    }

    void Retire() {
        u64 at = stamps_tail.load(std::memory_order_relaxed);
        const u64 head = stamps_head.load(std::memory_order_acquire);
        if (at == head) {
            // Refreshing the GPU cannot retire an as-yet unpublished stamp.
            ++recycle_empty_probes;
            return;
        }
        auto* semaphore = scheduler.GetWorkSemaphore();
        ++recycle_gpu_refreshes;
        semaphore->Refresh();
        while (at < head && semaphore->IsFree(stamps[at % stamps.size()].tick)) {
            retired = stamps[at % stamps.size()].end;
            ++at;
            ++recycle_retired_stamps;
        }
        stamps_tail.store(at, std::memory_order_release);
    }

    struct StampEntry {
        u64 end;
        u64 tick;
    };
    Scheduler& scheduler;
    VideoCore::Buffer buffer;
    u64 position = 0;     ///< stage A
    u64 retired = 0;      ///< stage A: the GPU is done with everything before this
    Detail::ConstantRingRecycleGate recycle_gate{}; ///< stage A: rate-limit Vulkan tick queries
    u64 recycle_probes = 0, recycle_skipped = 0, recycle_gpu_refreshes = 0;
    u64 recycle_empty_probes = 0, recycle_retired_stamps = 0, recycle_misses = 0;
    Detail::ConstantRingStampProgress stamp_progress{}; ///< stage B; tracks epoch even without new copies
    std::array<StampEntry, 1024> stamps{};
    alignas(64) std::atomic<u64> stamps_head{0}; ///< stage B writes
    alignas(64) std::atomic<u64> stamps_tail{0}; ///< stage A reads
};

} // namespace Vulkan
