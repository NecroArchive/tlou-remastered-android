// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
namespace VideoCore::Detail {
/// In a FIFO readback queue, the front tick is the oldest.
// A readback with tick >= CurrentTick belongs to a command buffer that
// the scheduler has not submitted yet: SubmitExecution uses NextTick() to
// advance the counter before submitting a signal for the old value.
// Consequently no readback in the queue can have reached the GPU, nor can
// a newly submitted read watch be installed for it.
[[nodiscard]] constexpr bool AllReadbacksUnsubmitted(
    std::uint64_t oldest_tick,std::uint64_t current_tick) noexcept {
    return oldest_tick>=current_tick;
}
}
