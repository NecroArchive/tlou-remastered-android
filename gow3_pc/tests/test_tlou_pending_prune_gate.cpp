// SPDX-License-Identifier: GPL-2.0-or-later
// Regression test for the actual Stage-A pending-prune gate and its completion
// transitions. Timing compares identical vector-prune workloads, not game FPS.
#include "video_core/renderer_vulkan/tlou_pending_prune_gate.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdlib>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <random>
#include <string_view>
#include <vector>

namespace {
using u64 = std::uint64_t;
constexpr u64 Uncommitted = std::numeric_limits<u64>::max();
struct Write {
    u64 begin, end, position;
    bool operator==(const Write&) const = default;
};
struct State {
    std::vector<Write> writes;
    std::vector<std::size_t> uncommitted_indices;
    Vulkan::Detail::PendingPruneGate gate;
    u64 skips = 0, scans = 0, erased = 0;

    void Add(u64 begin, u64 bytes, u64 pos) {
        const u64 end = begin + bytes;
        for (std::size_t i = 0; i < writes.size(); ++i) {
            auto& w = writes[i];
            if (begin <= w.end && w.begin <= end) {
                w.begin = std::min(w.begin, begin);
                w.end = std::max(w.end, end);
                w.position = pos;
                gate.Note(pos); // a merge can also shorten a packet position
                if (pos == Uncommitted) uncommitted_indices.push_back(i);
                return;
            }
        }
        if (pos == Uncommitted) uncommitted_indices.push_back(writes.size());
        writes.push_back({begin, end, pos});
        gate.Note(pos);
    }
    void Commit(u64 pos) {
        for (auto i : uncommitted_indices) {
            if (writes[i].position == Uncommitted) {
                writes[i].position = pos;
                gate.Note(pos); // the UINT64_MAX sentinel becomes a real head
            }
        }
        uncommitted_indices.clear();
    }
    void Prune(u64 completed, bool optimized) {
        if (!uncommitted_indices.empty() || writes.empty()) return;
        if (optimized && !gate.MayRetire(completed)) { ++skips; return; }
        ++scans;
        const std::size_t before = writes.size();
        if (optimized && gate.AllRetired(completed)) {
            writes.clear();
            erased += before;
            gate.Rebuild(writes);
            return;
        }
        std::erase_if(writes, [completed](const Write& w) {
            return w.position <= completed;
        });
        if (writes.size() != before) {
            erased += before - writes.size();
            if (optimized) gate.Rebuild(writes);
        }
    }
};

void Correctness() {
    State legacy, next;
    std::mt19937_64 rng{0x75fee28103};
    u64 head = 0, completed = 0;
    for (unsigned i = 0; i < 180000; ++i) {
        switch (rng() % 9) {
        case 0: case 1: case 2: case 3: case 4: {
            const u64 address = 0x2000000 + (rng() % 320) * 65536 +
                                ((rng() % 4 == 0) ? 64 : 0);
            const u64 bytes = 256 + rng() % 4096;
            const u64 position = (rng() % 7 == 0) ? Uncommitted : head;
            legacy.Add(address, bytes, position);
            next.Add(address, bytes, position);
            break;
        }
        case 5:
            // Stage A commits exactly the same packet after capturing all
            // constant snapshots. A new packet head cannot precede completed.
            head += 64 + (rng() % 128);
            legacy.Commit(head);
            next.Commit(head);
            break;
        case 6:
            // Stage B consumes forward but never beyond Stage A's head.
            if (head > completed) completed += rng() % (head - completed + 1);
            break;
        default:
            legacy.Prune(completed, false);
            next.Prune(completed, true);
            assert(legacy.writes == next.writes);
            assert(legacy.uncommitted_indices == next.uncommitted_indices);
            if (next.uncommitted_indices.empty() && !next.writes.empty()) {
                // The newest bound may be stale/higher after merges, but it
                // must NEVER be less than a pending real packet position.
                for (const auto& w : next.writes) {
                    assert(w.position <= next.gate.Newest());
                }
            }
            if ((i & 511) == 0 && !next.writes.empty()) {
                const auto oldest = std::min_element(next.writes.begin(), next.writes.end(),
                   [](const Write& a, const Write& b) { return a.position < b.position; });
                assert(next.gate.Oldest() <= oldest->position);
            }
            break;
        }
    }
    legacy.Commit(head + 128);
    next.Commit(head + 128);
    for (u64 progress = completed; progress <= head + 128; progress += 128) {
        legacy.Prune(progress, false);
        next.Prune(progress, true);
        assert(legacy.writes == next.writes);
    }
    legacy.Prune(head + 128, false);
    next.Prune(head + 128, true);
    assert(legacy.writes == next.writes);
    assert(next.writes.empty());
    assert(next.skips > 0 && next.scans > 0);
    // Sentinel -> committed packet end and then a full retirement must
    // allow O(1) clearing; stale maximums must reset for the next batch.
    State fast_clear;
    fast_clear.Add(0x1000, 64, Uncommitted);
    assert(!fast_clear.gate.AllRetired(0));
    fast_clear.Commit(300);
    assert(!fast_clear.gate.AllRetired(299));
    assert(fast_clear.gate.AllRetired(300));
    fast_clear.Prune(300, true);
    assert(fast_clear.writes.empty());
    assert(!fast_clear.gate.AllRetired(300));
    fast_clear.Add(0x2000, 64, 400);
    assert(!fast_clear.gate.AllRetired(300));
    std::printf("TLOU pending prune parity: 180000 interleaved events, "
                "merge/commit/retirement, skipped=%llu, scanned=%llu, erased=%llu passed\n",
                static_cast<unsigned long long>(next.skips),
                static_cast<unsigned long long>(next.scans),
                static_cast<unsigned long long>(next.erased));
}

static volatile u64 sink = 0;
double Benchmark(bool optimized, unsigned size) {
    State state;
    state.writes.reserve(size);
    // All writes belong to future packets: a full vector scan cannot remove
    // anything, but the existing implementation still runs erase_if.
    for (unsigned i = 0; i < size; ++i) {
        state.Add(0x10000000ull + u64(i) * 65536, 4096, 4096 + i);
    }
    const auto start = std::chrono::steady_clock::now();
    for (unsigned i = 0; i < 150000; ++i) {
        state.Prune(100, optimized);
    }
    sink = sink + state.writes.size() + state.skips + state.scans;
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
}

void Cost(bool gate) {
    for (unsigned size : {64u, 256u, 1024u}) {
        std::array<double, 7> before{}, after{};
        for (unsigned i = 0; i < 7; ++i) {
            if (i & 1) {
                after[i] = Benchmark(true, size);
                before[i] = Benchmark(false, size);
            } else {
                before[i] = Benchmark(false, size);
                after[i] = Benchmark(true, size);
            }
        }
        std::sort(before.begin(), before.end());
        std::sort(after.begin(), after.end());
        const double ratio = after[3] / before[3];
        std::printf("pending prune %u ranges: baseline=%.3fms gate=%.3fms "
                    "ratio=%.3f (host, no FPS inference)\n",
                    size, before[3], after[3], ratio);
        // Deterministic long-lived pending sets must benefit by at least 5x.
        // No guarantee is claimed for scenes where Stage B already retired all
        // entries; real skip/scanned counts are logged by the APK.
        if (gate && ratio >= 0.20) {
            std::fprintf(stderr, "CPU cost gate failed for %u pending ranges\n", size);
            std::abort();
        }
    }
}
} // namespace

int main(int argc, char** argv) {
    Correctness();
    if (argc == 1) { Cost(false); return 0; }
    if (argc == 2 && std::string_view(argv[1]) == "--cost-gate") {
        Cost(true);
        return 0;
    }
    return argc == 2 && std::string_view(argv[1]) == "--correctness-only" ? 0 : 2;
}
