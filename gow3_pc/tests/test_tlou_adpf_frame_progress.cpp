// SPDX-License-Identifier: GPL-2.0-or-later
// The REAL ADPF frame-progress helper must report completed monotonic work
// periods, never make one up after an unchanged GPU frame counter.
#include "video_core/renderer_vulkan/tlou_adpf_frame_progress.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
using Vulkan::Detail::TlouAdpfFrameProgress;
int main() {
    TlouAdpfFrameProgress frames;
    std::int64_t now = 1000000000LL;
    assert(frames.Sample(0,now)==0); // zero is a valid first frame ID
    for(int i=0;i<2000;i++) {
        now+=1000000; assert(frames.Sample(0,now)==0);
    }
    assert(frames.no_progress==2000);
    now+=16666667; assert(frames.Sample(1,now)==2016666667LL);
    now+=16666667; assert(frames.Sample(2,now)==16666667);
    now+=50000001; assert(frames.Sample(5,now)==16666667);
    assert(frames.completed_frames==5);
    now+=16666667; assert(frames.Sample(4,now)==0); // reset, no bogus report
    assert(frames.counter_resets==1);
    now+=16666667; assert(frames.Sample(5,now)==16666667);
    frames.ResetDiagnostics();
    assert(frames.completed_frames==0 && frames.no_progress==0 && frames.counter_resets==0);
    TlouAdpfFrameProgress many;
    std::int64_t tick=1000000;
    unsigned reports=0;
    for(std::uint64_t i=0;i<1000000;++i) {
        tick+=100000;
        const auto duration=many.Sample(i/7,tick);
        if(duration) {
            assert(duration>=100000 && duration<=700000);
            ++reports;
        }
    }
    assert(reports>140000);
    std::printf("TLOU ADPF monotonic cycles PASS; frames=%llu verified-reports=%u\n",
                (unsigned long long)many.completed_frames,reports);
}
