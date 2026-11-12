// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Android Dynamic Performance Framework (ADPF), native process implementation.
// Deliberately NOT a Java launcher hint: the game is a separate Bionic process,
// and a Java session on the launcher PID cannot boost its FEX/GPU threads.
// Every session contains only the currently executing GPU command thread TID.
//
// The API was added in Android 13 (NDK API 33). Resolve symbols at runtime so
// older devices and unsupported HALs silently keep the original scheduler.
#include "video_core/renderer_vulkan/tlou_adpf_frame_progress.h"
#include "video_core/renderer_vulkan/tlou_adpf_thread_set.h"
#ifdef __ANDROID__
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

struct APerformanceHintManager;
struct APerformanceHintSession;

namespace Vulkan::Detail {
class TlouAdpfCommandHint {
    using GetManager = APerformanceHintManager* (*)();
    using CreateSession = APerformanceHintSession* (*)(APerformanceHintManager*, const int32_t*,
                                                       size_t, int64_t);
    using Report = int (*)(APerformanceHintSession*, int64_t);
    using CloseSession = void (*)(APerformanceHintSession*);
    using SetThreads = int (*)(APerformanceHintSession*, const int32_t*, size_t);

    void* library = nullptr;
    APerformanceHintSession* session = nullptr;
    Report report = nullptr;
    CloseSession close = nullptr;
    SetThreads set_threads = nullptr;
    TlouAdpfThreadSet configured_threads{};
    int32_t command_tid = 0;
    bool thread_update_failed = false;
    TlouAdpfFrameProgress frames{};
    int64_t last_report_wall = 0;
    uint64_t reports = 0, failures = 0, attempts = 0, oversized = 0;
    unsigned callbacks = 0;
    bool started = false;
    bool enabled = false;
    int64_t target_ns = 16666666;

    static int64_t Nanos(clockid_t clock) {
        timespec ts{};
        if (clock_gettime(clock, &ts) != 0) return 0;
        return int64_t(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
    }
    void Start() {
        started = true;
        const char* flag = std::getenv("GOW3_ADPF");
        enabled = flag && flag[0] == '1' && flag[1] == '\0';
        if (!enabled) return;
        if (const char* value = std::getenv("GOW3_FPS_LIMIT")) {
            const int rate = std::atoi(value);
            if (rate == 30 || rate == 60 || rate == 120) target_ns = 1000000000LL / rate;
        }
        library = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
        if (!library) {
            std::printf("TLOU ADPF: unavailable (libandroid.so)\n");
            return;
        }
        const auto get = reinterpret_cast<GetManager>(dlsym(library,"APerformanceHint_getManager"));
        const auto create = reinterpret_cast<CreateSession>(
            dlsym(library, "APerformanceHint_createSession"));
        report = reinterpret_cast<Report>(dlsym(library, "APerformanceHint_reportActualWorkDuration"));
        close = reinterpret_cast<CloseSession>(dlsym(library, "APerformanceHint_closeSession"));
        // Android 14+ can add the stage-B draw and Vulkan recorder TIDs to the
        // SAME performance-hint workload; unsupported HALs retain one TID.
        set_threads = reinterpret_cast<SetThreads>(dlsym(library, "APerformanceHint_setThreads"));
        if (!get || !create || !report || !close) {
            std::printf("TLOU ADPF: unavailable (NDK hint API)\n");
            return;
        }
        auto* manager = get();
        const int32_t tid = int32_t(syscall(SYS_gettid));
        if (manager && tid > 0) session = create(manager, &tid, 1, target_ns);
        command_tid = tid;
        if (session) configured_threads = MakeTlouAdpfThreadSet(tid, 0, 0);
        std::printf("TLOU ADPF: %s tid=%d target=%lldns (native command thread)\n",
                    session ? "active" : "unsupported", int(tid), (long long)target_ns);
        last_report_wall = Nanos(CLOCK_MONOTONIC);
    }
public:
    TlouAdpfCommandHint() = default;
    TlouAdpfCommandHint(const TlouAdpfCommandHint&) = delete;
    TlouAdpfCommandHint& operator=(const TlouAdpfCommandHint&) = delete;
    ~TlouAdpfCommandHint() {
        if (session && close) close(session);
        if (library) dlclose(library);
    }
    void IncludeRenderingThreads(uint32_t draw_tid, uint32_t recorder_tid) {
        if (!session || !set_threads || thread_update_failed || command_tid <= 0 || !draw_tid) return;
        const auto wanted = MakeTlouAdpfThreadSet(command_tid, int32_t(draw_tid),
                                                    int32_t(recorder_tid));
        if (wanted.size < 2 || wanted == configured_threads) return;
        const int rc = set_threads(session, wanted.tids.data(), wanted.size);
        if (rc != 0) thread_update_failed = true; // Preserve the prior valid session on error.
        else configured_threads = wanted;
        std::printf("TLOU ADPF thread group: command=%d draw=%u recorder=%u "
                    "count=%zu rc=%d\\n",
                    command_tid, draw_tid, recorder_tid, wanted.size, rc);
    }
    void NotePostDraw(uint64_t current_frame) {
        if (!started) Start();
        // No new syscall or CPU clock work on 511 out of 512 packet calls.
        if (!session || (++callbacks & 511u)) return;

        // The Android NDK requires MONOTONIC durations for each completed
        // cycle. CPU-thread clock measured active CPU usage, NOT frame work
        // duration; use elapsed monotonic time between published frame IDs.
        const int64_t now = Nanos(CLOCK_MONOTONIC);
        const int64_t duration = frames.Sample(current_frame, now);
        if (duration > 0 && duration <= 1000000000LL) {
            ++attempts;
            const int rc = report(session, duration);
            if (rc == 0) ++reports;
            else ++failures;
        } else if (duration > 1000000000LL) {
            // Ignore stale loading gaps, rather than telling the governor
            // that a single gameplay frame consumed multiple seconds.
            ++oversized;
        }

        // The old code returned before the diagnostic when current_frame
        // failed to advance: a seemingly "active" session could send zero
        // reports forever, with no way to distinguish it from a healthy one.
        if (last_report_wall && now - last_report_wall >= 5000000000LL) {
            std::printf("TLOU ADPF (5 s): attempted=%llu reports=%llu errors=%llu "
                        "frame-advances=%llu no-frame-advance=%llu resets=%llu "
                        "oversized=%llu last-frame=%llu target=%lldns\n",
                        (unsigned long long)attempts,
                        (unsigned long long)reports,
                        (unsigned long long)failures,
                        (unsigned long long)frames.completed_frames,
                        (unsigned long long)frames.no_progress,
                        (unsigned long long)frames.counter_resets,
                        (unsigned long long)oversized,
                        (unsigned long long)frames.last_frame,
                        (long long)target_ns);
            std::fflush(stdout);
            reports = failures = attempts = oversized = 0;
            frames.ResetDiagnostics();
            last_report_wall = now;
        }
    }
};
}  // namespace Vulkan::Detail
#else
#include <cstdint>
namespace Vulkan::Detail {
class TlouAdpfCommandHint {
public:
    void NotePostDraw(std::uint64_t) {}
    void IncludeRenderingThreads(std::uint32_t, std::uint32_t) {}
};
}
#endif
