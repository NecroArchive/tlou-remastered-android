// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "common/types.h"

namespace VideoCore::Detail {

// The stream-copy reuse index is disabled by default. When disabled, neither
// the guest-write epoch nor the range hash affects any subsequent action.
// Preserve the scheduler tick for the ordinary stream stats if those stats
// are enabled, avoiding unnecessary guest-write-epoch calls for every V#.
//
// The callable form makes it possible to test that the epoch callback is
// *never* evaluated on the default path.
struct StreamCopyContext {
    u64 key=0;
    u64 tick=0;
    u64 epoch=0;
};

template<class Tick, class Epoch>
inline StreamCopyContext MakeStreamCopyContext(bool reuse, bool stats_enabled,
                                                u64 address, u32 size,
                                                Tick&& read_tick, Epoch&& read_epoch) {
    StreamCopyContext result;
    if (reuse) {
        result.key = address ^ (u64(size) << 44);
        result.tick = read_tick();
        result.epoch = read_epoch();
    } else if (stats_enabled) {
        result.tick = read_tick();
    }
    return result;
}
} // namespace VideoCore::Detail
