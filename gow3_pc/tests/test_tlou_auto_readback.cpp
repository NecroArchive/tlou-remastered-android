// SPDX-License-Identifier: GPL-2.0-or-later
#include "../gpu/shadps4/video_core/renderer_vulkan/tlou_auto_readback.h"
#include <cassert>
#include <initializer_list>
using TlouAutoReadback::Eligible;
// Only Full mode survives. Cover both the original 4272/6480-byte
// physics outputs and newly observed 4368-byte compute results.
static_assert(Eligible(true,true,false,4,1092,4368,0));
static_assert(Eligible(true,true,false,4,1068,4272,0));

static_assert(Eligible(true,true,false,4,1068,4272,0));
static_assert(Eligible(true,true,false,4,1620,6480,1));
static_assert(Eligible(true,true,false,16,256,4096,127));
static_assert(!Eligible(true,true,false,4,1620,6480,128));
static_assert(!Eligible(false,true,false,4,1620,6480,0)); // graphics writes excluded
static_assert(!Eligible(true,false,false,4,1620,6480,0)); // read-only
static_assert(!Eligible(true,true,true,4,1620,6480,0)); // existing exact path wins
static_assert(!Eligible(true,true,false,4,100,255,0));
static_assert(!Eligible(true,true,false,4,3000,12000,0));
static_assert(!Eligible(true,true,false,3,1068,3204,0));
static_assert(!Eligible(true,true,false,64,1,8192,0));
static_assert(!Eligible(true,true,false,4,1,8192,0));
static_assert(!Eligible(true,true,false,4,0,8192,0));
int main() {
    for (unsigned stride=1;stride<=72;++stride)
        for (unsigned bytes=0;bytes<=9000;bytes+=16)
            for(unsigned slots:{0u,127u,128u,255u}) {
                const bool expect=slots<128 && stride>=4 && stride<=64 &&
                    stride%4==0 && bytes>=256 && bytes<=8192;
                const unsigned records=stride? (bytes+stride-1)/stride:0;
                assert(Eligible(true,true,false,stride,records,bytes,slots)==expect);
            }
}
