// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace Vulkan::Detail {
// One ADPF session covers cooperating host threads; never pin affinities or
// include Java launcher TIDs. Exact ordering, deduplicated, max three threads.
struct TlouAdpfThreadSet {
    std::array<std::int32_t, 3> tids{};
    std::size_t size = 0;
    bool operator==(const TlouAdpfThreadSet&) const = default;
};
[[nodiscard]] constexpr TlouAdpfThreadSet MakeTlouAdpfThreadSet(
    std::int32_t command, std::int32_t draw, std::int32_t recorder) noexcept {
    TlouAdpfThreadSet result{};
    for (const auto tid : {command, draw, recorder}) {
        if (tid <= 0) continue;
        bool duplicate = false;
        for (std::size_t i=0; i<result.size; ++i) if (result.tids[i]==tid) duplicate=true;
        if (!duplicate) result.tids[result.size++] = tid;
    }
    return result;
}
} // namespace Vulkan::Detail
