/* SPDX-License-Identifier: GPL-2.0-or-later
 * Android critical-thread placement. Never widens the launch affinity mask.
 * Workers restore that mask instead of inheriting the critical-thread subset.
 */
#ifndef TLOU_ANDROID_CPU_POLICY_H
#define TLOU_ANDROID_CPU_POLICY_H
#ifdef __linux__
#include <sched.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <sys/syscall.h>

typedef struct { unsigned cpu; uint64_t capacity, max_khz; } TlCpuRank;
static inline int tlou_fast_cpu_set(const TlCpuRank *r, size_t count, cpu_set_t *out) {
    uint64_t min_c=UINT64_MAX,max_c=0,min_f=UINT64_MAX,max_f=0;
    CPU_ZERO(out);
    if (!count) return 0;
    for (size_t i=0;i<count;++i) {
        if (r[i].cpu>=CPU_SETSIZE) return 0;
        if (r[i].capacity<min_c) min_c=r[i].capacity;
        if (r[i].capacity>max_c) max_c=r[i].capacity;
        if (r[i].max_khz<min_f) min_f=r[i].max_khz;
        if (r[i].max_khz>max_f) max_f=r[i].max_khz;
    }
    const int metric=min_c && max_c>min_c ? 1 : min_f && max_f>min_f ? 2 : 0;
    uint64_t top=metric==1 ? max_c : max_f, second=0;
    unsigned selected=0;
    for (size_t i=0;i<count;++i) {
        uint64_t score=metric==1 ? r[i].capacity : r[i].max_khz;
        if (!metric || score==top) { CPU_SET(r[i].cpu,out);++selected; }
        else if (score>second) second=score;
    }
    // A single prime core must not serialize the main and recording threads.
    if (selected<2 && count>1) {
        for (size_t i=0;i<count;++i) {
            uint64_t score=metric==1 ? r[i].capacity : r[i].max_khz;
            if (score==second) CPU_SET(r[i].cpu,out);
        }
    }
    return metric;
}
static inline int tlou_cpu_mask_text(const cpu_set_t *set,char *out,size_t capacity) {
    size_t used=0;if (!capacity) return 0;out[0]=0;
    for (unsigned cpu=0;cpu<CPU_SETSIZE;++cpu) if (CPU_ISSET(cpu,set)) {
        int n=snprintf(out+used,capacity-used,"%s%u",used ? "," : "",cpu);
        if (n<0 || (size_t)n>=capacity-used) return 0;
        used+=(size_t)n;
    }
    return used!=0;
}
static inline int tlou_cpu_parse_mask(const char *text,cpu_set_t *out) {
    CPU_ZERO(out);
    if (!text || !*text) return 0;
    while (*text) {
        if (*text<'0' || *text>'9') return 0;
        char *end;long cpu=strtol(text,&end,10);
        if (end==text || cpu<0 || cpu>=CPU_SETSIZE || (*end && *end!=',')) return 0;
        CPU_SET((unsigned)cpu,out);
        if (*end && !end[1]) return 0;
        text=*end ? end+1 : end;
    }
    return CPU_COUNT(out)>0;
}
static inline int tlou_cpu_policy_enabled(void) {
    const char *value=getenv("TLOU_FAST_RENDER_CORES");
    return value && !strcmp(value,"1");
}
/* AOT-only shared-PRIME placement, replacing exclusive CPU6/CPU7 pins.
 * The toggle name is retained for saved preference/ABI compatibility; both
 * critical threads can migrate across allowed PRIME cores. Workers are never
 * narrowed by this option, and FEX is unchanged. */
static inline int tlou_aot_split_prime_enabled(void) {
    const char *value=getenv("TLOU_AOT_SPLIT_PRIME");
    const char *aot=getenv("BB_AOT");
    return aot && *aot && value && !strcmp(value,"1");
}
static inline int tlou_aot_split_prime_mask(const char *role,
                                            const cpu_set_t *fast,
                                            cpu_set_t *out) {
    if (!role || !fast || !out || CPU_COUNT(fast)!=2) return 0;
    // TLOU log 2(5): draw pinned to CPU6 coincided with 56.9 -> 32.8
    // flips/s, while GPU command dropped to ~38% and recorder stayed ~63%.
    // Exclusive masks make CPU6 a serial bottleneck even when CPU7 is free.
    // Both critical threads may use both PRIME cores; kernel schedules each
    // runnable thread. Never include cores outside the original launch mask.
    if (strcmp(role,"GPU command") && strcmp(role,"draw recording")) return 0;
    *out = *fast;
    return 1;
}
static inline uint64_t tlou_cpu_read(unsigned cpu,const char *field) {
    char path[192];snprintf(path,sizeof(path),"/sys/devices/system/cpu/cpu%u/%s",cpu,field);
    FILE *file=fopen(path,"r");if (!file) return 0;
    unsigned long long value=0;
    int valid=fscanf(file,"%llu",&value)==1;fclose(file);
    return valid ? (uint64_t)value : 0;
}
// The Java shell wrapper may not publish its pid on some Android builds.
// Publish from the real native runtime as well, once at startup (never per frame).
// The path is an app-private file supplied by GameActivity.
static inline void tlou_cpu_publish_native_pid(void) {
    const char *path=getenv("TLOU_NATIVE_PID_FILE");
    if (!path || !*path) return;
    FILE *file=fopen(path,"w");
    if (!file) return;
    fprintf(file,"%ld\\n",(long)getpid());
    fclose(file);
}
static inline void tlou_cpu_policy_prepare(void) {
    tlou_cpu_publish_native_pid();
    // Called once before FEX/GPU/helper threads exist. Environment is immutable afterwards.
    clockid_t clock;
    if (pthread_getcpuclockid(pthread_self(),&clock)==0) {
        char value[32];snprintf(value,sizeof(value),"%d",(int)clock);
        setenv("TLOU_MAIN_CPU_CLOCK",value,1);
    }
    if (!tlou_cpu_policy_enabled() && !tlou_aot_split_prime_enabled()) return;
    cpu_set_t allowed,fast;
    if (sched_getaffinity(0,sizeof(allowed),&allowed)!=0) {
        printf("TLOU CPU placement: cannot read launch mask errno=%d\n",errno);return;
    }
    TlCpuRank ranks[CPU_SETSIZE];size_t count=0;
    for (unsigned cpu=0;cpu<CPU_SETSIZE;++cpu) if (CPU_ISSET(cpu,&allowed)) {
        TlCpuRank rank={cpu,tlou_cpu_read(cpu,"cpu_capacity"),
                           tlou_cpu_read(cpu,"cpufreq/cpuinfo_max_freq")};
        ranks[count++]=rank;
    }
    int metric=tlou_fast_cpu_set(ranks,count,&fast);
    char baseline[CPU_SETSIZE*6],selected[CPU_SETSIZE*6];
    if (!tlou_cpu_mask_text(&allowed,baseline,sizeof(baseline)) ||
        !tlou_cpu_mask_text(&fast,selected,sizeof(selected))) return;
    if (setenv("TLOU_BASE_CPU_SET",baseline,1) || setenv("TLOU_FAST_CPU_SET",selected,1)) return;
    printf("TLOU CPU placement: allowed=%s critical=%s metric=%s shared-prime=%d; workers keep launch mask\n",
           baseline,selected,metric==1 ? "capacity" : metric==2 ? "maximum-frequency" : "unchanged/unknown",
           tlou_aot_split_prime_enabled());
}
static inline void tlou_cpu_policy_apply(const char *role,int critical) {
    const int legacy=tlou_cpu_policy_enabled();
    const int split=tlou_aot_split_prime_enabled();
    if(!legacy && !split)return;
    cpu_set_t baseline,wanted,current,fast;
    if (!tlou_cpu_parse_mask(getenv("TLOU_BASE_CPU_SET"),&baseline) ||
        !tlou_cpu_parse_mask(getenv("TLOU_FAST_CPU_SET"),&fast)) return;
    wanted=critical && legacy ? fast : baseline;
    cpu_set_t prime_choice;
    const int split_role=split && tlou_aot_split_prime_mask(role,&fast,&prime_choice);
    if(split_role) wanted=prime_choice;
    // Do not pin guest-main/worker/helper threads when only split mode is on.
    if(!legacy && !split_role)return;
    CPU_AND(&wanted,&wanted,&baseline);
    if (!CPU_COUNT(&wanted) || sched_getaffinity(0,sizeof(current),&current)) return;
    int result=0;
    if (memcmp(&current,&wanted,sizeof(current))) result=sched_setaffinity(0,sizeof(wanted),&wanted);
    int error=result ? errno : 0;
    if (role) {
        cpu_set_t actual;char text[CPU_SETSIZE*6];
        if (!sched_getaffinity(0,sizeof(actual),&actual) && tlou_cpu_mask_text(&actual,text,sizeof(text)))
            printf("TLOU CPU placement: %s tid=%ld actual=%s policy=%d nice-unchanged error=%d\n",
                   role,(long)syscall(SYS_gettid),text,sched_getscheduler(0),error);
    }
}
#endif
#endif
