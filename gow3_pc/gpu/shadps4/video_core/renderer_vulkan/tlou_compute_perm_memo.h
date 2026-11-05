// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// The cache is only a fast path for a compiled compute shader's permutation.
// A caller MUST refresh flattened guest descriptors before testing it, and
// MUST reject descriptors whose specialization reads a mutable external VMA.
// In particular this never caches the result of the SRT walker itself.
namespace Vulkan::Detail {
template<class RuntimeInfo>
class ExactComputePermutationMemo {
    static constexpr std::size_t MaxWords = 256;
    std::vector<std::uint32_t> flattened;
    RuntimeInfo runtime{};
    std::uint64_t base{};
    std::size_t permutation{};
    bool valid{false};
public:
    [[nodiscard]] bool Matches(std::uint64_t current_base,
                               std::span<const std::uint32_t> current_flat,
                               const RuntimeInfo& current_runtime,
                               std::size_t module_count) const {
        return valid && permutation < module_count &&
               base == current_base && runtime == current_runtime &&
               flattened.size() == current_flat.size() &&
               std::equal(flattened.begin(), flattened.end(), current_flat.begin());
    }
    [[nodiscard]] std::size_t Permutation() const noexcept {
        return permutation;
    }
    void Remember(std::uint64_t current_base,
                  std::span<const std::uint32_t> current_flat,
                  const RuntimeInfo& current_runtime, std::size_t index) {
        if (current_flat.size() > MaxWords) {
            Invalidate();
            return;
        }
        flattened.assign(current_flat.begin(), current_flat.end());
        base = current_base;
        runtime = current_runtime;
        permutation = index;
        valid = true;
    }
    void Invalidate() noexcept { valid = false; }
};
} // namespace Vulkan::Detail
