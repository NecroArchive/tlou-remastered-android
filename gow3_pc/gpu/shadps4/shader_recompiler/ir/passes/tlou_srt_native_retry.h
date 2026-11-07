// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <atomic>
#include <cstdint>
#include <limits>

namespace Shader::Arm64Srt {
// A transient bad SRT pointer must not permanently disable native ARM64 walkers.
// At the same time, a persistently invalid pointer must not trigger a SIGSEGV
// on every draw. Failed walkers use the existing fault-safe interpreter while
// this gate backs off attempts to resume native execution.
class NativeFaultRetry {
public:
    enum class Attempt : std::uint8_t { Skip, Fast, Probe };

    Attempt Begin(bool speculative) noexcept {
        if (healthy.load(std::memory_order_relaxed)) return Attempt::Fast;
        // A speculative caller has its own longjmp boundary. Do not start a
        // probe there, since its longjmp would bypass Success()/Fault().
        if (speculative) return Attempt::Skip;
        std::uint32_t count = cooldown.load(std::memory_order_relaxed);
        for (;;) {
            if (count == Probing) return Attempt::Skip;
            if (count == 0) {
                if (cooldown.compare_exchange_weak(count, Probing,
                        std::memory_order_acq_rel, std::memory_order_relaxed)) {
                    return Attempt::Probe;
                }
            } else if (cooldown.compare_exchange_weak(count, count - 1,
                           std::memory_order_acq_rel, std::memory_order_relaxed)) {
                return Attempt::Skip;
            }
        }
    }

    void Success() noexcept {
        failures.store(0, std::memory_order_relaxed);
        cooldown.store(0, std::memory_order_release);
        healthy.store(true, std::memory_order_release);
    }

    void Fault() noexcept {
        const auto count = failures.fetch_add(1, std::memory_order_relaxed);
        // 128, 256, 512 ... up to 32,768 interpreted calls per re-probe.
        const std::uint32_t shift = count < 8 ? count + 1 : 8;
        cooldown.store(64u << shift, std::memory_order_release);
        healthy.store(false, std::memory_order_release);
    }

    std::uint32_t Remaining() const noexcept {
        return cooldown.load(std::memory_order_relaxed);
    }

private:
    static constexpr std::uint32_t Probing = std::numeric_limits<std::uint32_t>::max();
    std::atomic<bool> healthy{true};
    std::atomic<std::uint32_t> cooldown{0};
    std::atomic<std::uint32_t> failures{0};
};
} // namespace Shader::Arm64Srt
