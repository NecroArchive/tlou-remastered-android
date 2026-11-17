#define _GNU_SOURCE
#include "../src/android_cpu_policy.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

/* Removed hardware-specific policies must never alter a thread's affinity,
 * regardless of old preference variables or selected AOT backend. */
int main(void) {
    cpu_set_t original,actual,unused;
    assert(sched_getaffinity(0,sizeof(original),&original)==0);
    assert(setenv("TLOU_FAST_RENDER_CORES","1",1)==0);
    assert(setenv("TLOU_AOT_SPLIT_PRIME","1",1)==0);
    assert(setenv("BB_AOT","/data/local/tmp/libtlouaot.so",1)==0);
    assert(setenv("TLOU_BASE_CPU_SET","0,1",1)==0);
    assert(tlou_cpu_policy_enabled()==0);
    assert(tlou_cpu_parse_mask("0,1",&unused)==0);
    tlou_cpu_policy_prepare();
    tlou_cpu_policy_apply("GPU command",1);
    tlou_cpu_policy_apply("draw recording",1);
    tlou_cpu_policy_apply(NULL,0);
    assert(sched_getaffinity(0,sizeof(actual),&actual)==0);
    assert(memcmp(&actual,&original,sizeof(original))==0);
    puts("Android CPU: no device-specific affinity/power policy applied");
    return 0;
}
