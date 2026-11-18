// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Vulkan::Detail {

// Exact number of 32-register blocks in RegDirty, counting marked bits one
// word at a time instead of materializing the whole block-number sequence.
// The existing ForEach() enumerates them in ascending order.
template <class Dirty>
[[nodiscard]] inline std::uint32_t CountDirtyBlocks(const Dirty& dirty) noexcept {
    std::uint32_t count = 0;
    for (std::uint64_t words = dirty.words_set; words; words &= words - 1) {
        count += std::popcount(dirty.blocks[std::countr_zero(words)]);
    }
    return count;
}

// Packet wire format is unchanged: [u16 ordered block IDs] [8-byte pad]
// [u32[32] register values for each ordered ID]. No guest snapshot is moved,
// delayed or merged. The dirty bitset is walked only after the size was
// determined; blocks are emitted directly into the destination packet.
template <class Dirty>
inline void WriteDirtyBlocks(const Dirty& dirty, const std::uint32_t* regs,
                             std::uint8_t* out, std::uint32_t count) noexcept {
    constexpr std::size_t BlockBytes = Dirty::BlockWords * sizeof(std::uint32_t);
    std::uint8_t* const values = out + ((count * sizeof(std::uint16_t) + 7u) & ~7u);
    std::uint32_t index = 0;
    dirty.ForEach([&](std::uint32_t block) {
        const std::uint16_t id = static_cast<std::uint16_t>(block);
        std::memcpy(out + index * sizeof(id), &id, sizeof(id));
        std::memcpy(values + std::size_t(index) * BlockBytes,
                    regs + std::size_t(block) * Dirty::BlockWords, BlockBytes);
        ++index;
    });
    assert(index == count);
}

} // namespace Vulkan::Detail
