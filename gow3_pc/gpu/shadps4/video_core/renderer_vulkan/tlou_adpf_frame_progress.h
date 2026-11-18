// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>

namespace Vulkan::Detail {

// Per-frame ADPF sampling must never invent progress: the native GPU command
// thread sees a frame counter published by the recording thread. Only a
// STRICTLY newer frame represents a completed work period. Unlike the old
// code, zero is a valid initial frame and skipping a report never suppresses
// the periodic diagnostic heartbeat.
class TlouAdpfFrameProgress {
public:
    std::uint64_t last_frame = 0;
    std::uint64_t completed_frames = 0;
    std::uint64_t no_progress = 0;
    std::uint64_t counter_resets = 0;

    // Return measured MONOTONIC elapsed wall time per advanced frame, 0
    // when no trustworthy new frame is available. Android requires
    // MONOTONIC clock for the NDK work-duration API.
    std::int64_t Sample(std::uint64_t frame, std::int64_t now) noexcept {
        if (now <= 0) return 0;
        if (!initialized || now <= last_time || frame < last_frame) {
            if (initialized && frame < last_frame) ++counter_resets;
            initialized = true;
            last_frame = frame;
            last_time = now;
            return 0;
        }
        if (frame == last_frame) {
            ++no_progress;
            return 0;
        }
        const std::uint64_t count = frame-last_frame;
        const std::int64_t elapsed = now-last_time;
        last_frame=frame;
        last_time=now;
        completed_frames += count;
        return elapsed / static_cast<std::int64_t>(count);
    }
    void ResetDiagnostics() noexcept {
        completed_frames = no_progress = counter_resets = 0;
    }
private:
    bool initialized = false;
    std::int64_t last_time = 0;
};
} // namespace Vulkan::Detail
