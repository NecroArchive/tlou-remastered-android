// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>

namespace AmdGpu::Detail {
// A count/time limit on *readback-producing events*, not on individual
// buffers or labels. External (frame, guest wait, fault) submits change
// the tick, so the next producer starts a fresh group automatically.
class EopBatchPolicy {
public:
    static constexpr std::uint32_t MaxEvents = 8;
    static constexpr std::uint64_t MaxAgeNs = 350000;
    bool Observe(std::uint64_t tick, std::uint64_t now_ns) noexcept {
        if (tick != tick_ || count_ == 0 || now_ns < first_ns_) {
            tick_ = tick;
            count_ = 0;
            first_ns_ = now_ns;
        }
        if (++count_ >= MaxEvents || now_ns - first_ns_ >= MaxAgeNs) {
            count_ = 0;
            return true;
        }
        return false;
    }
private:
    std::uint64_t tick_{}, first_ns_{};
    std::uint32_t count_{};
};
}
