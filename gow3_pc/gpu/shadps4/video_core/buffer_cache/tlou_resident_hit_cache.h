// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace VideoCore::Detail {
// Memoizes only PROVEN fully resident inclusive block intervals. No negative
// caching: fresh allocations always go through the original interval set.
// The owner must call Invalidate() after any resident block is removed.
class ResidentHitCache final {
public:
    bool Hit(std::uint64_t first, std::uint64_t last) const noexcept {
        const Entry& entry = entries[Slot(first, last)];
        return entry.generation == generation && entry.first == first && entry.last == last;
    }
    void Remember(std::uint64_t first, std::uint64_t last) noexcept {
        entries[Slot(first, last)] = {first, last, generation};
    }
    void Invalidate() noexcept {
        ++generation;
        if (generation == 0) {
            entries = {};
            generation = 1;
        }
    }
private:
    struct Entry {
        std::uint64_t first = 0, last = 0, generation = 0;
    };
    static constexpr std::size_t NumSlots = 256;
    static std::size_t Slot(std::uint64_t first, std::uint64_t last) noexcept {
        const std::uint64_t mixed = (first * UINT64_C(0x9e3779b97f4a7c15)) ^
                                    (last * UINT64_C(0xbf58476d1ce4e5b9));
        return static_cast<std::size_t>(mixed >> 56);
    }
    std::array<Entry, NumSlots> entries{};
    std::uint64_t generation = 1;
};
} // namespace VideoCore::Detail
