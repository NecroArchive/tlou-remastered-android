// SPDX-License-Identifier: GPL-2.0-or-later
// Exercise the production stream-key/epoch gate against the exact former
// unconditional scheduler reads. A disabled reuse mode must not read the
// guest epoch or construct an unused hash; a stream-mode hit must still keep
// both tick and write epoch, preserving snapshot coherency.
#include "video_core/buffer_cache/tlou_stream_copy_context.h"
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
using VideoCore::Detail::MakeStreamCopyContext;

int main() {
    std::mt19937_64 rng(0x20261110);
    for(bool reuse : {false,true})for(bool stats : {false,true}) {
        std::uint64_t ticks=0,epochs=0;
        std::uint64_t checksum=0;
        constexpr int Calls=1200000;
        for(int n=0;n<Calls;++n) {
            const u64 addr=rng() & 0xfffffffffull;
            const u32 size=u32(1+(rng()%16384));
            // Emulate values that would be returned by CurrentTick and
            // GuestWriteEpoch if the original unconditional reads ran.
            const u64 tick0=1234+(n/512),epoch0=999+n/19;
            const auto ctx=MakeStreamCopyContext(reuse,stats,addr,size,
                [&] { ++ticks; return tick0; },
                [&] { ++epochs; return epoch0; });
            assert(ctx.key == (reuse ? addr ^ (u64(size)<<44) : 0));
            assert(ctx.tick == (reuse||stats ? tick0 : 0));
            assert(ctx.epoch == (reuse ? epoch0 : 0));
            checksum ^= ctx.key ^ ctx.tick ^ ctx.epoch;
        }
        assert(ticks == (reuse||stats ? Calls : 0));
        assert(epochs == (reuse ? Calls : 0));
        std::printf("stream reuse=%d stats=%d: tick-reads=%llu epoch-reads=%llu "
                    "checksum=%llx PASS\n", reuse,stats,
                    (unsigned long long)ticks,
                    (unsigned long long)epochs,
                    (unsigned long long)checksum);
    }
}
