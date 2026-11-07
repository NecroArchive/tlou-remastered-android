// SPDX-License-Identifier: GPL-2.0-or-later
// The legacy prune rebuilt the bloom/bounds every 64th overlap query.
// Verify that skipping a no-op rebuild preserves overlap decisions.
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

struct Write { uint64_t begin, end, tick; };
struct Model {
    static constexpr unsigned Bits = 4096;
    std::vector<Write> writes;
    std::array<uint64_t, Bits / 64> bloom{};
    uint64_t lo = ~uint64_t(0), hi = 0;
    unsigned calls = 0, rebuilds = 0;
    void filter(uint64_t a, uint64_t b) {
        const uint64_t first = a >> 16, last = (b - 1) >> 16;
        if (last - first >= Bits) { bloom.fill(~uint64_t(0)); return; }
        for (uint64_t i = first; i <= last; ++i)
            bloom[(i % Bits) / 64] |= uint64_t(1) << (i % 64);
    }
    void add(Write w) {
        writes.push_back(w); lo = std::min(lo, w.begin);
        hi = std::max(hi, w.end); filter(w.begin, w.end);
    }
    void rebuild() {
        lo = ~uint64_t(0); hi = 0; bloom.fill(0);
        for (const auto& w : writes) {
            lo = std::min(lo, w.begin); hi = std::max(hi, w.end);
            filter(w.begin, w.end);
        }
        ++rebuilds;
    }
    void prune(uint64_t tick, bool optimized) {
        if ((++calls & 63) != 0) return;
        const size_t before = writes.size();
        std::erase_if(writes, [&](const Write& w) { return w.tick <= tick; });
        if (!optimized || writes.size() != before) rebuild();
    }
    bool overlaps(uint64_t a, uint64_t size, uint64_t tick) const {
        if (writes.empty() || a >= hi || a + size <= lo) return false;
        const uint64_t first = a >> 16, last = (a + size - 1) >> 16;
        bool hit = last - first >= Bits;
        if (!hit) for (uint64_t i = first; i <= last; ++i)
            hit |= (bloom[(i % Bits) / 64] & (uint64_t(1) << (i % 64))) != 0;
        if (!hit) return false;
        for (const auto& w : writes)
            if (a < w.end && w.begin < a + size && w.tick > tick) return true;
        return false;
    }
};
int main() {
    std::mt19937_64 rng(0x20261107ULL);
    unsigned saved = 0, tested = 0;
    for (unsigned run = 0; run < 400; ++run) {
        Model original, optimized;
        for (unsigned n = 0, total = 1 + rng() % 2048; n < total; ++n) {
            const uint64_t a = (rng() % 8000000) * 4096;
            const Write w{a, a + 1 + rng() % 131072, rng() % 1000000};
            original.add(w); optimized.add(w);
        }
        for (unsigned step = 0; step < 1300; ++step) {
            if (rng() % 16 == 0) {
                const uint64_t a = (rng() % 8000000) * 4096;
                const Write w{a, a + 1 + rng() % 131072, rng() % 1000000};
                original.add(w); optimized.add(w);
            }
            const uint64_t tick = (step / 40) * 20000;
            original.prune(tick, false);
            optimized.prune(tick, true);
            assert(original.writes.size() == optimized.writes.size());
            assert(original.lo == optimized.lo && original.hi == optimized.hi);
            // An unchanged bloom must never miss a write in the baseline.
            for (unsigned k = 0; k < original.bloom.size(); ++k)
                assert((original.bloom[k] & optimized.bloom[k]) == original.bloom[k]);
            for (unsigned k = 0; k < 2; ++k) {
                const uint64_t a = (rng() % 8000000) * 4096;
                assert(original.overlaps(a, 131072, tick) ==
                       optimized.overlaps(a, 131072, tick));
                ++tested;
            }
        }
        saved += original.rebuilds - optimized.rebuilds;
    }
    std::printf("pending prune: %u matched overlap queries, %u needless bloom rebuilds eliminated\\n",
                tested, saved);
}
