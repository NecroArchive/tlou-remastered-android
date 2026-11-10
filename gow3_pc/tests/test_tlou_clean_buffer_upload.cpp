// SPDX-License-Identifier: GPL-2.0-or-later
// The production clean read-only buffer gate must not skip GPU-written
// buffers, image aliases, disabled-lock-free fallbacks, or CPU-dirty pages.
#include "video_core/buffer_cache/tlou_clean_buffer_upload.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <random>

int main() {
    std::mt19937 rng{0x20261110};
    std::uint64_t skipped=0, upload_ranges=0, guard_checks=0;
    for(unsigned i=0;i<3000000;++i) {
        const bool written=(rng()%100)<35;
        const bool texel=(rng()%100)<10;
        const bool optimized=(rng()%100)<95;
        const bool dirty=(rng()%100)<25;
        unsigned calls=0;
        const bool skip=VideoCore::Detail::CanSkipCleanBufferUpload(
            written,texel,optimized,[&]{++calls;return dirty;});
        const bool expected=!written&&!texel&&optimized&&!dirty;
        assert(skip==expected);
        const bool should_probe=!written&&!texel&&optimized;
        assert(calls==(should_probe?1u:0u));
        guard_checks+=calls;
        skipped+=skip;
        // Preserve whether the original upload path can possibly upload
        // guest bytes or mark GPU-written page bits.
        const bool work_before=written || texel || dirty;
        const bool work_after=skip?false:work_before;
        assert(work_before==work_after);
        upload_ranges+=work_after;
    }
    for(int w=0;w<2;++w)for(int t=0;t<2;++t)for(int enabled=0;enabled<2;++enabled)
        for(int dirty=0;dirty<2;++dirty) {
            const bool fast=VideoCore::Detail::CanSkipCleanBufferUpload(
                w,t,enabled,[&]{return bool(dirty);});
            assert(fast==(!w&&!t&&enabled&&!dirty));
        }
    std::printf("TLOU clean buffer upload: 3M exact decisions PASS; "
                "avoided-empty-upload-walks=%llu; probes=%llu "
                "actual-write-or-texel-ranges=%llu\n",
                (unsigned long long)skipped,
                (unsigned long long)guard_checks,
                (unsigned long long)upload_ranges);
}
