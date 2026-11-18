// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "video_core/amdgpu/pm4_opcodes.h"
#include <cstdint>
namespace Vulkan::Detail {
// Only these classes can omit ProcessCompute's unconditional PRE-drain.
// DMA_DATA and other guest-visible synchronization remain intact; memory
// WAIT_REG_MEM takes a separate deferred-wait path that preserves the predicate
// and the existing pending-fence/deferred-readback checks.
enum class ComputePreDrain : std::uint8_t { Required, InnerDrain, NestedBuffer };
// Only the compute-memory wait may defer its pre-drain, NEVER a register wait.
// It must then run the existing guarded compute wait implementation (not a
// direct guest Test() read without pending-fence or deferred-readback checks).
[[nodiscard]] constexpr bool CanDeferComputeMemoryWait(AmdGpu::PM4ItOpcode op,
                                                        bool memory_wait,
                                                        bool enabled) noexcept {
    return enabled && memory_wait && op == AmdGpu::PM4ItOpcode::WaitRegMem;
}
constexpr ComputePreDrain ClassifyComputePreDrain(AmdGpu::PM4ItOpcode op) noexcept {
    using AmdGpu::PM4ItOpcode;
    switch (op) {
    // ACQUIRE_MEM and EVENT_WRITE deliberately fall through to Required:
    // even if their case bodies currently do nothing, the PRE-drain may be
    // their only host-side ordering guarantee for previously queued commands.
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
    case ComputePreDrain::InnerDrain:
        return true;
    case ComputePreDrain::NestedBuffer:
        return safe_ib_enabled && !pending_ib_write;
    default:
        return false;
    }
}
} // namespace Vulkan::Detail
