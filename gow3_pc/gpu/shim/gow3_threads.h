// SPDX-License-Identifier: GPL-2.0-or-later
// gow3: helper thread sizing. Counts follow the hardware threads this process may run on
// (the affinity mask, so `taskset` can emulate a Steam Deck), and speculative helpers run as
// SCHED_IDLE: they use cores the game leaves idle and never take time from its threads.

#pragma once

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#ifdef __ANDROID__
#include "../../src/android_cpu_policy.h"
#endif
#ifdef _WIN32
#include <bit>
#include <windows.h>
#else
#include <sched.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace Gow3Threads {
#ifdef _WIN32
/// Hardware threads available to the process (its affinity mask).
inline unsigned Available() {
    DWORD_PTR process = 0, system = 0;
    if (GetProcessAffinityMask(GetCurrentProcess(), &process, &system) && process) {
        return std::max(1, std::popcount(static_cast<unsigned long long>(process)));
    }
    return std::max(1u, std::thread::hardware_concurrency());
}

/// The calling thread only runs when cores are otherwise idle (lowest Windows priority).
inline void MakeBackground() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_IDLE);
}

/// GOW3_GPU_THREAD_PRIORITY=above|high for the GPU command thread and, since 21 Oct 2026, the
/// draw and Vulkan recording threads. At normal priority they shared cores with the guest's
/// busy job workers in some sessions for the whole session (The Last of Us Remastered: 33-34
/// instead of 46-47 FPS in 5 of 11 runs, these threads at ~49% of a core instead of ~67%;
/// "above": 47 FPS in 4 runs of 4).
inline void ApplyGpuThreadPriority(const char* name) {
    const char* env = std::getenv("GOW3_GPU_THREAD_PRIORITY");
    if (!env || !*env) {
        return;
    }
    const int priority = !std::strcmp(env, "high")    ? THREAD_PRIORITY_HIGHEST
                         : !std::strcmp(env, "above") ? THREAD_PRIORITY_ABOVE_NORMAL
                                                      : THREAD_PRIORITY_NORMAL;
    SetThreadPriority(GetCurrentThread(), priority);
    std::printf("%s: priority %s\n", name, env);
}
#else

/// Hardware threads available to the process.
inline unsigned Available() {
    cpu_set_t set;
#ifdef __ANDROID__
    // Helper sizing must use the launch mask, not a critical thread's subset.
    if ((tlou_cpu_policy_enabled() || tlou_aot_cortex_render_enabled()) &&
        tlou_cpu_parse_mask(std::getenv("TLOU_BASE_CPU_SET"), &set))
        return std::max(1, CPU_COUNT(&set));
#endif
    CPU_ZERO(&set);
    if (sched_getaffinity(0, sizeof(set), &set) == 0) {
        return std::max(1, CPU_COUNT(&set));
    }
    return std::max(1u, std::thread::hardware_concurrency());
}

/// The calling thread only runs on otherwise idle cores (falls back to the lowest nice level).
inline void MakeBackground() {
#ifdef __ANDROID__
    tlou_cpu_policy_apply(nullptr,0);
#endif
    sched_param param{};
    if (sched_setscheduler(0, SCHED_IDLE, &param) != 0) {
        setpriority(PRIO_PROCESS, static_cast<id_t>(gettid()), 19);
    }
}
#endif

} // namespace Gow3Threads
