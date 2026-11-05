// SPDX-License-Identifier: GPL-2.0-or-later
// Standalone native x86-64/ARM64 test of the *production* draw-pipe busy-timer
// helper. No Vulkan, Android APK or fabricated game-FPS measurements.
#include "../gpu/shadps4/video_core/renderer_vulkan/vk_draw_pipe_busy_sample.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {
using Clock = std::chrono::steady_clock;
constexpr std::size_t Packets = 300'000;
constexpr std::size_t Repeats = 9;
volatile std::uint64_t escaped = 0;

inline void HandlePacket(std::uint64_t& state, std::size_t i) {
    state ^= (static_cast<std::uint64_t>(i) + 0x9e3779b97f4a7c15ULL);
    state = state * 2862933555777941757ULL + 3037000493ULL;
    // Stops folding across packet boundaries but does not issue a CPU fence.
    std::atomic_signal_fence(std::memory_order_seq_cst);
}

void Correctness() {
    std::atomic<u64> cycles{17};
    assert(Vulkan::Detail::BeginDrawPipeBusySample(false) == 0);
    Vulkan::Detail::EndDrawPipeBusySample(false, 0, cycles);
    assert(cycles.load() == 17); // No reads and no atomic modification.
    for (std::size_t i = 0; i < 1000; ++i) {
        const auto before = Vulkan::Detail::BeginDrawPipeBusySample(true);
        std::uint64_t state = i + 1;
        HandlePacket(state, i);
        Vulkan::Detail::EndDrawPipeBusySample(true, before, cycles);
        escaped = state;
    }
    assert(cycles.load() > 17);
}

enum class Mode { LegacyAlways, NewDisabled, NewEnabled };

double Run(Mode mode, std::size_t count) {
    std::atomic<u64> cycles{0};
    std::uint64_t state = 0x123456789abcdefULL;
    const auto started = Clock::now();
    if (mode == Mode::LegacyAlways) {
        for (std::size_t i = 0; i < count; ++i) {
            const auto begin = Common::FencedRDTSC();
            HandlePacket(state, i);
            cycles.fetch_add(Common::FencedRDTSC() - begin, std::memory_order_relaxed);
        }
    } else if (mode == Mode::NewDisabled) {
        for (std::size_t i = 0; i < count; ++i) {
            const auto begin = Vulkan::Detail::BeginDrawPipeBusySample(false);
            HandlePacket(state, i);
            Vulkan::Detail::EndDrawPipeBusySample(false, begin, cycles);
        }
        assert(cycles.load(std::memory_order_relaxed) == 0);
    } else {
        for (std::size_t i = 0; i < count; ++i) {
            const auto begin = Vulkan::Detail::BeginDrawPipeBusySample(true);
            HandlePacket(state, i);
            Vulkan::Detail::EndDrawPipeBusySample(true, begin, cycles);
        }
        assert(cycles.load(std::memory_order_relaxed) > 0);
    }
    escaped = state;
    return std::chrono::duration<double, std::nano>(Clock::now() - started).count()
           / static_cast<double>(count);
}

template<std::size_t N> double Median(std::array<double, N> values) {
    std::sort(values.begin(), values.end());
    return values[N / 2];
}
} // namespace

int main(int argc, char** argv) {
    Correctness();
    if (argc > 1 && std::strcmp(argv[1], "--correctness-only") == 0) {
        std::puts("draw-pipe busy timer correctness: PASS");
        return 0;
    }
    for (int i = 0; i < 3; ++i) {
        Run(Mode::LegacyAlways, 10'000);
        Run(Mode::NewDisabled, 10'000);
        Run(Mode::NewEnabled, 10'000);
    }
    std::array<double, Repeats> legacy{}, disabled{}, enabled{};
    for (std::size_t rep = 0; rep < Repeats; ++rep) {
        // Alternate order to limit systematic DVFS/thermal bias.
        if ((rep & 1) == 0) {
            legacy[rep] = Run(Mode::LegacyAlways, Packets);
            disabled[rep] = Run(Mode::NewDisabled, Packets);
            enabled[rep] = Run(Mode::NewEnabled, Packets);
        } else {
            enabled[rep] = Run(Mode::NewEnabled, Packets);
            disabled[rep] = Run(Mode::NewDisabled, Packets);
            legacy[rep] = Run(Mode::LegacyAlways, Packets);
        }
    }
    const double a = Median(legacy), b = Median(disabled), c = Median(enabled);
    const double save = a - b;
    std::printf("draw-pipe packets: %zu, repeats: %zu\n", Packets, Repeats);
    std::printf("legacy always ns/packet: %.3f\n", a);
    std::printf("stats disabled ns/packet: %.3f\n", b);
    std::printf("stats enabled ns/packet: %.3f\n", c);
    std::printf("OFF saving ns/packet: %.3f\n", save);
    std::printf("OFF saving per 1000 packets: %.6f ms (synthetic; not device FPS)\n",
                save / 1000.0);
    std::puts("For a game frame: saving_ms = saving_ns_per_packet * measured_packets_per_frame / 1e6.");
    return std::isfinite(save) ? 0 : 1;
}
