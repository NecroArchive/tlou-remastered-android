/* SPDX-License-Identifier: GPL-2.0-or-later
 * EXPERIMENT ONLY: make AOT-only prime splitting effective in the pinned TLOU
 * native APK without replacing its proven FEX/GPU binaries.
 *
 * The launcher sends TLOU_AOT_SPLIT_PRIME=1, but the release-packaging workflow
 * explicitly preserves an older gow3-probe and libgow3gpu. Their newer thread
 * policy functions are not compiled into that binary. The AOT libgow3cpu.so is
 * already LD_PRELOADed for AOT worker-stack compatibility. Interpose the real
 * name-setting call used by Common::SetCurrentThreadName in libgow3gpu,
 * applying affinity only after naming the TWO critical threads succeeds.
 * FEX is untouched. No APK should be published without on-device A/B evidence.
 */
#include "android_cpu_policy.h"
#include <dlfcn.h>
#include <pthread.h>
#include <cstring>
#include <cstdio>

__attribute__((constructor)) static void init_aot_affinity() {
    // The process receives BB_AOT and TLOU_AOT_SPLIT_PRIME before dlopen.
    if (tlou_aot_split_prime_enabled()) tlou_cpu_policy_prepare();
}

extern "C" __attribute__((visibility("default")))
int pthread_setname_np(pthread_t thread, const char* name) {
    using Original = int (*)(pthread_t, const char*);
    static Original original = reinterpret_cast<Original>(
        dlsym(RTLD_NEXT, "pthread_setname_np"));
    if (!original) return ENOSYS;
    const int rc = original(thread, name);
    if (rc || !pthread_equal(thread, pthread_self()) ||
        !tlou_aot_split_prime_enabled()) return rc;
    // Linux allows 15 visible bytes; only the two known names are selected.
    const char* role = nullptr;
    if (!std::strncmp(name, "shadPS4:GpuComm", 15)) role = "GPU command";
    if (!std::strncmp(name, "gow3:DrawRec", 12)) role = "draw recording";
    if (role) tlou_cpu_policy_apply(role, 1);
    return rc;
}
