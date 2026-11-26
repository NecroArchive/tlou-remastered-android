// SPDX-License-Identifier: GPL-2.0-or-later
#include "../gpu/shadps4/video_core/amdgpu/tlou_eop_batch_policy.h"
#include <cassert>
using AmdGpu::Detail::EopBatchPolicy;
int main() {
    EopBatchPolicy policy;
    for(unsigned i=0;i<EopBatchPolicy::MaxEvents-1;++i) assert(!policy.Observe(1,1000+i*100));
    assert(policy.ObserveLimit(1,2300)==EopBatchPolicy::Limit::Count);
    for(unsigned i=0;i<EopBatchPolicy::MaxEvents-1;++i) assert(!policy.Observe(2,2000+i*100));
    // External submit closes the old group; no stale count on the next tick.
    assert(!policy.Observe(3,3000));
    assert(!policy.Observe(3,352999));
    assert(policy.ObserveLimit(3,353000)==EopBatchPolicy::Limit::Age);
    // The first producer after a forced flush starts a fresh group.
    assert(!policy.Observe(4,360000));
    assert(!policy.Observe(4,361000));
    assert(!policy.Observe(5,362000));
    // Reset safely on a backwards synthetic clock.
    assert(!policy.Observe(5,1));
    assert(policy.Observe(5,350001));
}
