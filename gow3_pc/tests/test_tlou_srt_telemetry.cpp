// SPDX-License-Identifier: GPL-2.0-or-later
// Standalone compile/execution test of the real CPU SRT telemetry helper.
#include "../gpu/shadps4/shader_recompiler/ir/passes/tlou_srt_telemetry.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

using Perf = Shader::Arm64Srt::SrtTelemetry;
int main() {
    assert(setenv("TLOU_SRT_PROFILE", "1", 1) == 0);
    assert(Perf::Enabled());
    Perf::JitCreated(true);
    Perf::JitCreated(false);
    std::vector<std::thread> workers;
    for (unsigned w = 0; w < 4; ++w) {
        workers.emplace_back([w] {
            for (unsigned i = 0; i < 32768; ++i) {
                Perf::Call call{w == 0 ? Perf::Path::Native
                    : w == 1 ? Perf::Path::InterpreterNoJit
                    : w == 2 ? Perf::Path::InterpreterCooldown
                             : Perf::Path::InterpreterAfterFault};
                if (w == 0 && i == 0) call.Probing();
                if (w == 0 && i == 1) call.Recovered();
                if (w == 3 && i == 0) call.Faulted();
                if ((i & 7u) == 0) {
                    Perf::Prewalk prewalk;
                    if ((i & 31u) == 0) prewalk.Download(2);
                }
                call.Done(100 + i / 512);
            }
        });
    }
    for (auto& worker : workers) worker.join();
    std::puts("TLOU SRT telemetry: native/interpreted paths, concurrent updates and CPU clocks OK");
}
