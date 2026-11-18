// SPDX-License-Identifier: GPL-2.0-or-later
#include "video_core/renderer_vulkan/tlou_debug_marker_gate.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>

int main() {
    using Vulkan::Detail::MarkerNeedsRecording;
    for (bool guest_enabled : {false, true}) {
        for (bool host_enabled : {false, true}) {
            assert(MarkerNeedsRecording(true, guest_enabled, host_enabled) == guest_enabled);
            assert(MarkerNeedsRecording(false, guest_enabled, host_enabled) == host_enabled);
        }
    }
    assert(!MarkerNeedsRecording(true, false, false));
    assert(!MarkerNeedsRecording(false, false, false));
    puts("TLOU disabled Vulkan marker: no draw-pipe drain on either path PASS");
}
