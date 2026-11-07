// SPDX-License-Identifier: GPL-2.0-or-later
#include "video_core/renderer_vulkan/tlou_adpf.h"
#include <cassert>
#include <cstdio>

int main() {
    using Vulkan::TlouAdpf::FrameCadence;
    FrameCadence frames(16'666'667);
    assert(frames.Target() == 16'666'667);
    assert(frames.Next(1'000'000'000LL) == 0);
    assert(frames.Next(1'016'666'667LL) == 16'666'667);
    assert(frames.Next(1'017'000'000LL) == 0); // duplicate display callback
    assert(frames.Next(1'033'333'334LL) == 16'666'667);
    assert(frames.Next(1'500'000'000LL) == 0); // long loading/pause
    assert(frames.Next(1'525'000'000LL) == 25'000'000);
    assert(frames.Next(1'524'000'000LL) == 0); // monotonic regression
    assert(frames.Next(1'540'666'667LL) == 16'666'667);
    std::puts("TLOU ADPF frame boundaries, 30/60 targets and pause guard: PASS");
}
