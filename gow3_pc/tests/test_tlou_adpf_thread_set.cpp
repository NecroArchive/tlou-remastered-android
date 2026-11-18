// SPDX-License-Identifier: GPL-2.0-or-later
// Exact native ADPF thread-group composition, no Android API required.
#include "video_core/renderer_vulkan/tlou_adpf_thread_set.h"
#include <cassert>
#include <cstdio>
using Vulkan::Detail::MakeTlouAdpfThreadSet;
static_assert(MakeTlouAdpfThreadSet(11,22,33).size==3);
static_assert(MakeTlouAdpfThreadSet(11,22,11).size==2);
static_assert(MakeTlouAdpfThreadSet(11,0,0).size==1);
static_assert(MakeTlouAdpfThreadSet(-1,0,0).size==0);
int main(){
    auto a=MakeTlouAdpfThreadSet(100,101,102);
    assert(a.size==3 && a.tids[0]==100 && a.tids[1]==101 && a.tids[2]==102);
    assert(MakeTlouAdpfThreadSet(100,101,102)==a);
    assert(MakeTlouAdpfThreadSet(100,101,101).size==2);
    assert(MakeTlouAdpfThreadSet(100,0,102).size==2);
    assert(MakeTlouAdpfThreadSet(100,100,100).size==1);
    puts("TLOU ADPF session TID grouping: command/draw/recorder; zero and duplicate IDs PASS");
}
