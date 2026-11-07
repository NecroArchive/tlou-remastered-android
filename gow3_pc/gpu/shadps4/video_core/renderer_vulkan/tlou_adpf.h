// SPDX-License-Identifier: GPL-2.0-or-later
// Android frame-deadline hints for the three long-lived native GPU threads.
// No priority/affinity forcing and no changes to Vulkan or guest-memory ordering.
#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#ifdef __ANDROID__
#include <dlfcn.h>
#include <sys/types.h>
#include <mutex>
#endif

namespace Vulkan::TlouAdpf {

// Pure frame cadence decision; tested on x86-64 and ARM64 without Android.
class FrameCadence {
public:
    explicit FrameCadence(int64_t target_ns) : target{target_ns} {}

    [[nodiscard]] int64_t Next(int64_t now_ns) {
        if (now_ns <= 0) return 0;
        if (!last) { last = now_ns; return 0; }
        if (now_ns <= last) { last = now_ns; return 0; }
        const int64_t elapsed = now_ns - last;
        if (elapsed > 250'000'000) {
            last = now_ns; // paused, suspended or loading; not a frame cost
            return 0;
        }
        if (elapsed < 4'000'000) {
            return 0; // duplicate display callback, don't report a false-fast frame
        }
        last = now_ns;
        return elapsed;
    }

    [[nodiscard]] int64_t Target() const { return target; }

private:
    int64_t target;
    int64_t last = 0;
};

#ifdef __ANDROID__
// All optional symbols are resolved at runtime: APK also supports Android 11/12.
// Native sessions are required: the Java launcher lives in a different process
// from FEX/GPU, while ADPF only permits TIDs of the session's own process.
class FrameHints {
public:
    FrameHints() : cadence([] {
        const char* limit = std::getenv("GOW3_FPS_LIMIT");
        return limit && limit[0] == '3' && limit[1] == '0' && limit[2] == '\0'
                   ? 33'333'333LL : 16'666'667LL;
    }()) {}

    ~FrameHints() {
        if (session && close_session) close_session(session);
        if (library) dlclose(library);
    }

    void OnFrame(int32_t command_tid, int32_t draw_tid, int32_t recorder_tid) {
        static const bool wanted = [] {
            const char* env = std::getenv("TLOU_ADPF");
            return env && env[0] == '1' && env[1] == '\0';
        }();
        if (!wanted) return;
        std::scoped_lock lock{mutex};
        if (failed) return;

        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        const int64_t stamp = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
        const int64_t elapsed = cadence.Next(stamp);
        bool created = false;
        if (!session) {
            if (command_tid <= 0 || !Load()) return;
            const auto next = UniqueTids(command_tid, draw_tid, recorder_tid);
            session = create_session(get_manager(), next.data(), Count(next), cadence.Target());
            if (!session) {
                std::printf("TLOU ADPF: device rejected CPU performance hint session; original scheduling retained\n");
                failed = true;
                return;
            }
            tids = next;
            created = true;
            std::printf("TLOU ADPF: session active command=%d draw=%d recorder=%d target=%.2fms\n",
                        command_tid, draw_tid, recorder_tid, cadence.Target() / 1e6);
            std::fflush(stdout);
        }
        const auto next = UniqueTids(command_tid, draw_tid, recorder_tid);
        if (next != tids && set_threads && !thread_update_failed) {
            // Late-starting stage B/Vulkan recorder can join a live session.
            if (set_threads(session, next.data(), Count(next)) == 0) {
                tids = next;
            } else {
                thread_update_failed = true;
                std::printf("TLOU ADPF: thread update unavailable; original thread group retained\n");
            }
        }
        // One report per completed frame, never per draw/dispatch. No device
        // overclocking: Android's power/thermal policy makes the decision.
        if (elapsed > 0 && !created && report_work(session, elapsed) != 0) {
            std::printf("TLOU ADPF: hint reporting unavailable; renderer unaffected\n");
            failed = true;
        }
    }

private:
    static std::array<int32_t, 3> UniqueTids(int32_t command, int32_t draw, int32_t recorder) {
        std::array<int32_t, 3> out{command, 0, 0};
        unsigned n = 1;
        if (draw > 0 && draw != command) out[n++] = draw;
        if (recorder > 0 && recorder != command && recorder != draw) out[n++] = recorder;
        return out;
    }
    static size_t Count(const std::array<int32_t, 3>& ts) {
        return size_t(ts[0] != 0) + size_t(ts[1] != 0) + size_t(ts[2] != 0);
    }
    bool Load() {
        library = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
        if (!library) { failed = true; return false; }
        get_manager = reinterpret_cast<GetManager>(dlsym(library, "APerformanceHint_getManager"));
        create_session = reinterpret_cast<CreateSession>(dlsym(library, "APerformanceHint_createSession"));
        report_work = reinterpret_cast<ReportWork>(dlsym(library, "APerformanceHint_reportActualWorkDuration"));
        close_session = reinterpret_cast<CloseSession>(dlsym(library, "APerformanceHint_closeSession"));
        set_threads = reinterpret_cast<SetThreads>(dlsym(library, "APerformanceHint_setThreads"));
        if (!get_manager || !create_session || !report_work || !close_session || !get_manager()) {
            std::printf("TLOU ADPF: native CPU performance hints unsupported; original scheduler retained\n");
            failed = true;
            return false;
        }
        return true;
    }
    using GetManager = void* (*)();
    using CreateSession = void* (*)(void*, const int32_t*, size_t, int64_t);
    using ReportWork = int (*)(void*, int64_t);
    using CloseSession = void (*)(void*);
    using SetThreads = int (*)(void*, const int32_t*, size_t);
    FrameCadence cadence;
    std::mutex mutex;
    void* library = nullptr;
    void* session = nullptr;
    GetManager get_manager = nullptr;
    CreateSession create_session = nullptr;
    ReportWork report_work = nullptr;
    CloseSession close_session = nullptr;
    SetThreads set_threads = nullptr;
    std::array<int32_t, 3> tids{};
    bool failed = false;
    bool thread_update_failed = false;
};

inline void OnFrame(int32_t command_tid, int32_t draw_tid, int32_t recorder_tid) {
    static FrameHints session;
    session.OnFrame(command_tid, draw_tid, recorder_tid);
}
#endif
} // namespace Vulkan::TlouAdpf
