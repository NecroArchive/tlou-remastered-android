// SPDX-License-Identifier: GPL-2.0-or-later
// Standalone correctness/concurrency contract for the production SRT retry gate.
// Build: c++ -std=c++20 -O2 -pthread -I ../gpu/shadps4/shader_recompiler/ir/passes test_tlou_srt_native_retry.cpp
#include "../gpu/shadps4/shader_recompiler/ir/passes/tlou_srt_native_retry.h"
#include <cassert>
#include <atomic>
#include <thread>
#include <vector>

using Retry = Shader::Arm64Srt::NativeFaultRetry;

int main() {
    Retry retry;
    assert(retry.Begin(false) == Retry::Attempt::Fast);
    assert(retry.Begin(true) == Retry::Attempt::Fast);
    retry.Fault();
    assert(retry.Remaining() == 128);
    for (int i = 0; i < 128; ++i) {
        assert(retry.Begin(true) == Retry::Attempt::Skip);
        assert(retry.Begin(false) == Retry::Attempt::Skip);
    }
    assert(retry.Begin(false) == Retry::Attempt::Probe);
    assert(retry.Begin(false) == Retry::Attempt::Skip); // one probing thread
    retry.Fault();
    assert(retry.Remaining() == 256);
    for (int i = 0; i < 256; ++i) assert(retry.Begin(false) == Retry::Attempt::Skip);
    assert(retry.Begin(false) == Retry::Attempt::Probe);
    retry.Success();
    assert(retry.Begin(false) == Retry::Attempt::Fast);
    retry.Fault();
    assert(retry.Remaining() == 128); // successful probe resets backoff
    for (int i = 0; i < 128; ++i) assert(retry.Begin(false) == Retry::Attempt::Skip);
    std::atomic<int> probes{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) threads.emplace_back([&] {
        if (retry.Begin(false) == Retry::Attempt::Probe) ++probes;
    });
    for (auto& t : threads) t.join();
    assert(probes == 1);
    retry.Success();
    assert(retry.Begin(true) == Retry::Attempt::Fast);
}
