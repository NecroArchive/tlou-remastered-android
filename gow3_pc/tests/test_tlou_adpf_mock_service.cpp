// SPDX-License-Identifier: GPL-2.0-or-later
// Fake NDK service for a host-native ABI and control-flow test only.
#include <cstddef>
#include <cstdint>
static int manager, session, report_count, threads_count;
extern "C" {
void* APerformanceHint_getManager() { return &manager; }
void* APerformanceHint_createSession(void*, const int32_t* tids, size_t count, int64_t target) {
    if (!tids || count == 0 || target < 16'000'000 || target > 34'000'000) return nullptr;
    threads_count = int(count);
    return &session;
}
int APerformanceHint_reportActualWorkDuration(void*, int64_t duration) {
    if (duration < 4'000'000 || duration > 250'000'000) return -1;
    ++report_count; return 0;
}
int APerformanceHint_setThreads(void*, const int32_t*, size_t count) {
    threads_count = int(count); return 0;
}
void APerformanceHint_closeSession(void*) {}
int tlou_mock_reports() { return report_count; }
int tlou_mock_threads() { return threads_count; }
}
