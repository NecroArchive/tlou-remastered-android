// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>

namespace AmdGpu::Detail {
// A count/time limit on *readback-producing events*, not on individual
// buffers or labels. External (frame, guest wait, fault) submits change
// the tick, so the next producer starts a fresh group automatically.
class EopBatchPolicy {
public:
    static constexpr std::uint32_t MaxEvents = 24;
    static constexpr std::uint64_t MaxAgeNs = 700000;
    enum class Limit { None, Count, Age };
    Limit ObserveLimit(std::uint64_t tick, std::uint64_t now_ns) noexcept {
        if (tick != tick_ || count_ == 0 || now_ns < first_ns_) {
            tick_ = tick;
            count_ = 0;
            first_ns_ = now_ns;
        }
        ++count_;
        const auto limit = now_ns - first_ns_ >= MaxAgeNs ? Limit::Age :
                           count_ >= MaxEvents ? Limit::Count : Limit::None;
        if (limit != Limit::None) count_ = 0;
        return limit;
    }
    bool Observe(std::uint64_t tick, std::uint64_t now_ns) noexcept {
        return ObserveLimit(tick,now_ns) != Limit::None;
    }
private:
    std::uint64_t tick_{}, first_ns_{};
    std::uint32_t count_{};
};
}
