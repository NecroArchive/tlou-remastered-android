// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <limits>

namespace Vulkan::Detail {

// Only the known TLOU 64-wide scalar shader's fully readable, aligned large
// linear copies are eligible. Callers separately check mappings, descriptor
// format, swizzle and stencil surfaces before recording a transfer.
[[nodiscard]] constexpr bool EligibleLargeScalarCopy(
    std::uint64_t hash, std::uint64_t expected_hash,
    std::uint64_t count, std::uint64_t readable,
    std::uint64_t src, std::uint64_t dst, std::uint64_t bytes) noexcept {
    if (hash != expected_hash || count != readable ||
        bytes <= 65536 || bytes > std::numeric_limits<std::uint32_t>::max() ||
        src > std::numeric_limits<std::uint64_t>::max() - bytes ||
        dst > std::numeric_limits<std::uint64_t>::max() - bytes) {
        return false;
    }
    return src <= dst ? dst - src >= bytes : src - dst >= bytes;
}

// Vulkan disallows overlapping source/destination regions of one VkBuffer.
// Test by subtraction, not addition, to avoid overflow at large offsets.
[[nodiscard]] constexpr bool NonOverlappingVulkanCopy(
    bool same_buffer, std::uint64_t src_offset,
    std::uint64_t dst_offset, std::uint64_t bytes) noexcept {
    return !same_buffer ||
           (src_offset <= dst_offset
                ? dst_offset - src_offset >= bytes
                : src_offset - dst_offset >= bytes);
}
} // namespace Vulkan::Detail
