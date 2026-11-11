// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <utility>

namespace Vulkan::Detail {

// A single Stage-A producer may wait for Stage-B consumed progress.
// Atomic waits park only after a short bounded active spin; the consumer
// publishes progress in the same global seq_cst order as registration.
// This prevents lost wakes even when Stage B advances between registering
// as waiting and the next consumed load.
inline void PublishPipeProgress(std::atomic<std::uint64_t>& completed,
                                std::atomic<bool>& producer_waiting,
                                std::uint64_t position) noexcept {
    completed.store(position, std::memory_order_seq_cst);
    if (producer_waiting.load(std::memory_order_seq_cst)) {
        completed.notify_one();
    }
}

template <class Ready, class Pause>
inline void WaitForPipeProgress(std::atomic<std::uint64_t>& completed,
                                std::atomic<bool>& producer_waiting,
                                Ready&& ready, Pause&& pause,
                                std::uint64_t& park_count,
                                std::uint64_t& parked_ns,
                                std::uint32_t spin_limit = 4096) {
    std::uint32_t spins = 0;
    for (;;) {
        std::uint64_t seen = completed.load(std::memory_order_acquire);
        if (ready(seen)) return;
        if (spins++ < spin_limit) {
            pause();
            continue;
        }
        producer_waiting.store(true, std::memory_order_seq_cst);
        seen = completed.load(std::memory_order_seq_cst);
        if (!ready(seen)) {
            ++park_count;
            const auto start = std::chrono::steady_clock::now();
            completed.wait(seen, std::memory_order_acquire);
            parked_ns += static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - start).count());
        }
        producer_waiting.store(false, std::memory_order_seq_cst);
        spins = 0;
    }
}

} // namespace Vulkan::Detail
