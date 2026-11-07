// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Opt-in ARM64 SRT telemetry: exact path counters, sparse per-thread CPU clocks
// and a five-second summary. SRT is CPU work; Vulkan timestamp measurements
// are separately provided by the sampled GPU profiler.
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace Shader::Arm64Srt {
class SrtTelemetry {
public:
    enum class Path : unsigned { Native, InterpreterNoJit, InterpreterCooldown, InterpreterAfterFault, Count };
    static constexpr unsigned Paths = static_cast<unsigned>(Path::Count);

    static bool Enabled() {
        static const bool on = [] {
            const char* env = std::getenv("TLOU_SRT_PROFILE");
            return env && env[0] == '1' && env[1] == '\0';
        }();
        return on;
    }

    static void JitCreated(bool native) {
        if (!Enabled()) return;
        (native ? jit_created : jit_unavailable).fetch_add(1, std::memory_order_relaxed);
    }

    class Call {
    public:
        explicit Call(Path path) : enabled(Enabled()), mode(path) {
            if (!enabled) return;
            static thread_local std::uint32_t sequence = 0;
            sampled = ((++sequence & 255u) == 0);
            if (sampled) started = CpuNs();
        }
        void Faulted() {
            mode = Path::InterpreterAfterFault;
            if (enabled) native_faults.fetch_add(1, std::memory_order_relaxed);
        }
        void Probing() {
            if (enabled) retry_attempts.fetch_add(1, std::memory_order_relaxed);
        }
        void Recovered() {
            if (enabled) recovered.fetch_add(1, std::memory_order_relaxed);
        }
        void Done(std::uint64_t gpu_frames) {
            if (!enabled) return;
            const unsigned index = static_cast<unsigned>(mode);
            counts[index].fetch_add(1, std::memory_order_relaxed);
            if (sampled) {
                cpu_ns[index].fetch_add(CpuNs() - started, std::memory_order_relaxed);
                cpu_samples[index].fetch_add(1, std::memory_order_relaxed);
            }
            const auto calls = total_calls.fetch_add(1, std::memory_order_relaxed) + 1;
            if ((calls & 8191u) == 0) Report(gpu_frames);
        }
    private:
        bool enabled = false, sampled = false;
        Path mode;
        std::uint64_t started = 0;
    };

private:
    static std::uint64_t CpuNs() {
        timespec now{};
        if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now) != 0) return 0;
        return static_cast<std::uint64_t>(now.tv_sec) * 1000000000ull +
               static_cast<std::uint64_t>(now.tv_nsec);
    }
    static void Report(std::uint64_t frames_now) {
        using Clock = std::chrono::steady_clock;
        const auto time = std::chrono::duration_cast<std::chrono::nanoseconds>(
                              Clock::now().time_since_epoch()).count();
        std::int64_t previous = last_report_ns.load(std::memory_order_relaxed);
        if (!previous) {
            last_report_ns.compare_exchange_strong(previous, time, std::memory_order_relaxed);
            previous_gpu_frames.store(frames_now, std::memory_order_relaxed);
            return;
        }
        if (time - previous < 5000000000ll ||
            !last_report_ns.compare_exchange_strong(previous, time,
                                                     std::memory_order_acq_rel)) return;
        const std::uint64_t before = previous_gpu_frames.exchange(frames_now, std::memory_order_relaxed);
        const std::uint64_t frames = frames_now >= before ? frames_now - before : 0;
        std::array<std::uint64_t, Paths> n{}, samples{}, times{};
        std::uint64_t all = 0;
        double estimate_ns = 0.0;
        for (unsigned i = 0; i < Paths; ++i) {
            n[i] = counts[i].exchange(0, std::memory_order_relaxed);
            samples[i] = cpu_samples[i].exchange(0, std::memory_order_relaxed);
            times[i] = cpu_ns[i].exchange(0, std::memory_order_relaxed);
            all += n[i];
            if (samples[i]) estimate_ns += double(n[i]) * double(times[i]) / double(samples[i]);
        }
        // A CPU-thread-clock sample captures only this function, not Stage A's
        // caller and not the GPU. The sum is cumulative CPU cost across threads.
        std::printf(
            "TLOU SRT ARM64 (5s): total=%llu native=%llu interp-nojit=%llu "
            "interp-cooldown=%llu interp-fault=%llu native-faults=%llu "
            "retry-attempts=%llu recovered=%llu jit-ok=%llu jit-missing=%llu "
            "frames=%llu approx-cpu-ms/frame=%.3f "
            "native-sample-us=%.3f/%llu nojit-sample-us=%.3f/%llu "
            "cooldown-sample-us=%.3f/%llu fault-sample-us=%.3f/%llu\n",
            (unsigned long long)all,
            (unsigned long long)n[0], (unsigned long long)n[1],
            (unsigned long long)n[2], (unsigned long long)n[3],
            (unsigned long long)native_faults.exchange(0, std::memory_order_relaxed),
            (unsigned long long)retry_attempts.exchange(0, std::memory_order_relaxed),
            (unsigned long long)recovered.exchange(0, std::memory_order_relaxed),
            (unsigned long long)jit_created.exchange(0, std::memory_order_relaxed),
            (unsigned long long)jit_unavailable.exchange(0, std::memory_order_relaxed),
            (unsigned long long)frames,
            frames ? estimate_ns / (1e6 * double(frames)) : 0.0,
            samples[0] ? double(times[0]) / (1e3 * double(samples[0])) : 0.0, (unsigned long long)samples[0],
            samples[1] ? double(times[1]) / (1e3 * double(samples[1])) : 0.0, (unsigned long long)samples[1],
            samples[2] ? double(times[2]) / (1e3 * double(samples[2])) : 0.0, (unsigned long long)samples[2],
            samples[3] ? double(times[3]) / (1e3 * double(samples[3])) : 0.0, (unsigned long long)samples[3]);
        std::fflush(stdout);
    }

    static inline std::atomic<std::uint64_t> total_calls{};
    static inline std::atomic<std::uint64_t> counts[Paths]{};
    static inline std::atomic<std::uint64_t> cpu_ns[Paths]{};
    static inline std::atomic<std::uint64_t> cpu_samples[Paths]{};
    static inline std::atomic<std::uint64_t> jit_created{};
    static inline std::atomic<std::uint64_t> jit_unavailable{};
    static inline std::atomic<std::uint64_t> native_faults{};
    static inline std::atomic<std::uint64_t> retry_attempts{};
    static inline std::atomic<std::uint64_t> recovered{};
    static inline std::atomic<std::int64_t> last_report_ns{};
    static inline std::atomic<std::uint64_t> previous_gpu_frames{};
};
} // namespace Shader::Arm64Srt
