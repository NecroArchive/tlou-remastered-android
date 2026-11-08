// SPDX-License-Identifier: GPL-2.0-or-later
// Diagnostic-only, opt-in: measure actual ShaderProgram::GetParams frequency
// on the gameplay thread(s). Never cache guest instructions or suppress reads.
#pragma once
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <thread>
namespace AmdGpu::Detail {
class ShaderScanProfiler {
public:
    struct Stats {
        std::uint64_t calls=0, direct=0, fallback=0;
        std::array<std::uint64_t,5> offsets{};
        std::uint64_t samples=0, scalar_ns=0, neon_ns=0, mismatches=0;
    };
    [[nodiscard]] const Stats& Snapshot() const noexcept { return stats_; }
    template <typename Scalar, typename Neon>
    void Observe(const std::uint32_t* code, const std::uint32_t* selected, bool direct,
                 Scalar&& scalar, Neon&& neon) {
        ++stats_.calls;
        if (direct) {
            ++stats_.direct;
        } else {
            ++stats_.fallback;
            const std::size_t offset=static_cast<std::size_t>(selected-code);
            const std::size_t bucket = offset<=128 ? 0 : offset<=1024 ? 1 :
                                       offset<=4096 ? 2 : offset<=8192 ? 3 : 4;
            ++stats_.offsets[bucket];
            if ((stats_.fallback & 255u) == 0) {
                // The original full search and NEON examine the same CURRENT
                // shader bytes. Alternate execution order to limit cache bias.
                using clock=std::chrono::steady_clock;
                const bool reverse=(stats_.samples & 1u)!=0;
                const auto a=clock::now();
                const auto* first=reverse?neon():scalar();
                const auto b=clock::now();
                const auto* second=reverse?scalar():neon();
                const auto c=clock::now();
                const auto t0=std::chrono::duration_cast<std::chrono::nanoseconds>(b-a).count();
                const auto t1=std::chrono::duration_cast<std::chrono::nanoseconds>(c-b).count();
                stats_.scalar_ns+=static_cast<std::uint64_t>(reverse?t1:t0);
                stats_.neon_ns+=static_cast<std::uint64_t>(reverse?t0:t1);
                stats_.mismatches+=(first!=selected || second!=selected);
                ++stats_.samples;
            }
        }
        // One clock read at most per 1024 GetParams calls, not per draw.
        if ((stats_.calls & 1023u) == 0) ReportIfDue();
    }
    void ReportIfDue() {
        using clock=std::chrono::steady_clock;
        const auto now=clock::now();
        if (now-last_report_ < std::chrono::seconds(5)) return;
        if (!last_report_.time_since_epoch().count()) {
            last_report_=now;
            return;
        }
        const auto& s=stats_;
        const double saved_ms = double(std::int64_t(s.scalar_ns)-std::int64_t(s.neon_ns))*256.0/1e6;
        std::fprintf(stdout,
            "TLOU SHADER_SCAN CPU 5s: thread=%zu calls=%llu direct=%llu fallback=%llu "
            "offsets=<=128:%llu,129-1024:%llu,1025-4096:%llu,4097-8192:%llu,>8192:%llu "
            "sampled=%llu scalar_ns=%llu neon_ns=%llu estimated_saved_ms_5s=%.4f mismatches=%llu\n",
            std::hash<std::thread::id>{}(std::this_thread::get_id()),
            static_cast<unsigned long long>(s.calls),
            static_cast<unsigned long long>(s.direct),
            static_cast<unsigned long long>(s.fallback),
            static_cast<unsigned long long>(s.offsets[0]),
            static_cast<unsigned long long>(s.offsets[1]),
            static_cast<unsigned long long>(s.offsets[2]),
            static_cast<unsigned long long>(s.offsets[3]),
            static_cast<unsigned long long>(s.offsets[4]),
            static_cast<unsigned long long>(s.samples),
            static_cast<unsigned long long>(s.scalar_ns),
            static_cast<unsigned long long>(s.neon_ns),
            saved_ms,static_cast<unsigned long long>(s.mismatches));
        std::fflush(stdout);
        stats_={};
        last_report_=now;
    }
private:
    Stats stats_{};
    std::chrono::steady_clock::time_point last_report_{std::chrono::steady_clock::now()};
};
} // namespace AmdGpu::Detail
