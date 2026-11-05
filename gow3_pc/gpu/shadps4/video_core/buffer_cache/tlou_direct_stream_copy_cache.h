// SPDX-License-Identifier: GPL-2.0-or-later
// Experimental, opt-in same-submission stream copy lookup.
// Reuses the production stream-cache validity contract; cache collisions
// may lose hits but must never produce a stale hit.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include "common/types.h"

namespace VideoCore {

template<std::size_t Capacity=8192>
class DirectStreamCopyCache {
    static_assert(Capacity && (Capacity & (Capacity-1))==0);
    struct Entry {
        u64 key{},tick{},epoch{},wraps{},offset{};
        bool valid=false;
    };
    std::array<Entry,Capacity> entries{};
    static std::size_t Slot(u64 key) noexcept {
        key ^= key >> 33;
        key *= 0xff51afd7ed558ccdull;
        key ^= key >> 33;
        key *= 0xc4ceb9fe1a85ec53ull;
        key ^= key >> 33;
        return static_cast<std::size_t>(key) & (Capacity-1);
    }
public:
    [[nodiscard]] std::optional<u64> Find(u64 key,u64 tick,u64 epoch,u64 wraps) const noexcept {
        const auto& e=entries[Slot(key)];
        if (e.valid && e.key==key && e.tick==tick && e.epoch==epoch && e.wraps==wraps)
            return e.offset;
        return std::nullopt;
    }
    void Put(u64 key,u64 tick,u64 epoch,u64 wraps,u64 offset) noexcept {
        entries[Slot(key)] = {key,tick,epoch,wraps,offset,true};
    }
    void Clear() noexcept { for(auto& e:entries)e.valid=false; }
};

} // namespace VideoCore
