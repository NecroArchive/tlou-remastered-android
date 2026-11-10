// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>

namespace Vulkan::Detail {

// Compute dispatches frequently reuse an already compiled pipeline. The GPU
// command thread may take that exact pipeline from the cache without polling
// every asynchronous build on every single dispatch, as long as it still
// services background work periodically and always services it for misses.
//
// Shader-key construction is never skipped and per-dispatch data snapshots
// remain at the original point in guest command order.
class ComputeCachedHitServiceGate {
public:
    static constexpr unsigned ServiceInterval = 64;

    // The first lookup is ALWAYS serviced, as is every 64th thereafter.
    // Any pending compiler/prefetch result forces immediate service.
    [[nodiscard]] bool TryCachedHit(bool has_pending_work) noexcept {
        if (has_pending_work || since_service >= ServiceInterval - 1) {
            since_service = 0;
            return false;
        }
        ++since_service;
        return true;
    }

    // An absent or invalid pipeline must run the original publication path.
    void OnCacheMiss() noexcept {
        since_service = 0;
    }

private:
    unsigned since_service = ServiceInterval - 1;
};

} // namespace Vulkan::Detail
