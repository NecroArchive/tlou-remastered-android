/* SPDX-License-Identifier: GPL-2.0-or-later
 * Android process identity and CPU-clock publication only.
 * Device-specific CPU ranking, PRIME splitting, and affinity overrides were
 * removed. The Android scheduler controls all gameplay threads unchanged.
 */
#ifndef TLOU_ANDROID_CPU_POLICY_H
#define TLOU_ANDROID_CPU_POLICY_H
#ifdef __linux__
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sched.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static inline int tlou_cpu_policy_enabled(void) { return 0; }

/* Keep the two legacy callers ABI/source compatible but do not re-apply old
 * masks even when a preference or inherited environment variable survives. */
static inline int tlou_cpu_parse_mask(const char *text,cpu_set_t *out) {
    (void)text;
    CPU_ZERO(out);
    return 0;
}
static inline void tlou_cpu_policy_apply(const char *role,int critical) {
    (void)role;
    (void)critical;
}
static inline void tlou_cpu_publish_native_pid(void) {
    const char *path=getenv("TLOU_NATIVE_PID_FILE");
    if (!path || !*path) return;
    FILE *file=fopen(path,"w");
    if (!file) return;
    fprintf(file,"%ld\n",(long)getpid());
    fclose(file);
}
static inline void tlou_cpu_policy_prepare(void) {
    tlou_cpu_publish_native_pid();
    /* Preserve CPU-clock reporting used by generic FPS/CPU HUD diagnostics. */
    clockid_t clock;
    if (pthread_getcpuclockid(pthread_self(),&clock)==0) {
        char value[32];
        snprintf(value,sizeof(value),"%d",(int)clock);
        setenv("TLOU_MAIN_CPU_CLOCK",value,1);
    }
}
#endif
#endif
