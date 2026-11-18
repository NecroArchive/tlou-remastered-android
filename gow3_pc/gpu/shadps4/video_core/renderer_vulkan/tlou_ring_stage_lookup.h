// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
namespace Vulkan::Detail {
// A RingStage is selected by pointer equality once per BindBuffers stage.
// On the non-cursor path, search ONLY its bindings. Exact first-match order
// is retained, including duplicate binding indices and an empty stage.
template <class Stage>
inline auto FindPacketRingBinding(const Stage* stage, std::uint32_t index) noexcept
    -> decltype(stage->bindings) {
    if (!stage) return nullptr;
    for (std::uint32_t i=0;i<stage->count;++i)
        if (stage->bindings[i].index==index) return &stage->bindings[i];
    return nullptr;
}
} // namespace Vulkan::Detail
