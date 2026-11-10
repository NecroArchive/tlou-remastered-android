// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Android Dynamic Performance Framework (ADPF), native process implementation.
// Deliberately NOT a Java launcher hint: the game is a separate Bionic process,
// and a Java session on the launcher PID cannot boost its FEX/GPU threads.
// Every session contains only the currently executing GPU command thread TID.
//
// The API was added in Android 13 (NDK API 33). Resolve symbols at runtime so
// older devices and unsupported HALs silently keep the original scheduler.
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

    void* library = nullptr;
    APerformanceHintSession* session = nullptr;
    Report report = nullptr;
    CloseSession close = nullptr;
    uint64_t last_frame = 0;
    int64_t last_cpu = 0;
    int64_t last_report_wall = 0;
    uint64_t reports = 0, failures = 0;
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
        if (!get || !create || !report || !close) {
            std::printf("TLOU ADPF: unavailable (NDK hint API)\n");
            return;
        }
        auto* manager = get();
        const int32_t tid = int32_t(syscall(SYS_gettid));
        if (manager && tid > 0) session = create(manager, &tid, 1, target_ns);
        std::printf("TLOU ADPF: %s tid=%d target=%lldns (native command thread)\n",
                    session ? "active" : "unsupported", int(tid), (long long)target_ns);
        last_cpu = Nanos(CLOCK_THREAD_CPUTIME_ID);
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
    void NotePostDraw(uint64_t current_frame) {
        if (!started) Start();
        if (!session || (++callbacks & 511u)) return;
        if (!last_frame) {
            last_frame = current_frame;
            last_cpu = Nanos(CLOCK_THREAD_CPUTIME_ID);
            return;
        }
        if (current_frame <= last_frame) return;
        const int64_t cpu = Nanos(CLOCK_THREAD_CPUTIME_ID);
        const uint64_t frames = current_frame - last_frame;
        if (cpu > last_cpu && frames > 0) {
            const int64_t duration = (cpu - last_cpu) / int64_t(frames);
            if (duration > 0) {
                const int rc = report(session, duration);
                if (rc == 0) ++reports;
                else ++failures;
            }
        }
        last_cpu = cpu;
        last_frame = current_frame;
        const int64_t now = Nanos(CLOCK_MONOTONIC);
        if (last_report_wall && now-last_report_wall >= 5000000000LL) {
            std::printf("TLOU ADPF (5 s): reports=%llu errors=%llu frame-hints=yes\n",
                        (unsigned long long)reports,(unsigned long long)failures);
            reports = failures = 0;
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
};
}
#endif
