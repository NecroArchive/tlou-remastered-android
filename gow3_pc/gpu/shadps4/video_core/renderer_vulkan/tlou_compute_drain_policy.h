// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "video_core/amdgpu/pm4_opcodes.h"
#include <cstdint>
namespace Vulkan::Detail {
// Only these classes can omit ProcessCompute's unconditional PRE-drain.
// WAIT_REG_MEM, DMA_DATA and other guest-visible synchronization remain intact.
enum class ComputePreDrain : std::uint8_t { Required, NoEffect, InnerDrain, NestedBuffer };
constexpr ComputePreDrain ClassifyComputePreDrain(AmdGpu::PM4ItOpcode op) noexcept {
    using AmdGpu::PM4ItOpcode;
    switch (op) {
    case PM4ItOpcode::AcquireMem: // currently no-op in the compute interpreter
    case PM4ItOpcode::EventWrite: // currently no-op in the compute interpreter
        return ComputePreDrain::NoEffect;
    case PM4ItOpcode::WriteData:  // WaitHostCopies already drains before touching guest memory
        return ComputePreDrain::InnerDrain;
    case PM4ItOpcode::IndirectBuffer: // recursive packet parser, no Vulkan operation itself
        return ComputePreDrain::NestedBuffer;
    default:
        return ComputePreDrain::Required;
    }
}
[[nodiscard]] constexpr bool CanSkipComputePreDrain(ComputePreDrain kind, bool pending_ib_write,
                                                     bool safe_ib_enabled) noexcept {
    switch (kind) {
    case ComputePreDrain::NoEffect:
    case ComputePreDrain::InnerDrain:
        return true;
    case ComputePreDrain::NestedBuffer:
        return safe_ib_enabled && !pending_ib_write;
    default:
        return false;
    }
}
} // namespace Vulkan::Detail
