// SPDX-License-Identifier: GPL-2.0-or-later
// Exact production positive-residency memo vs authoritative resident set.
#include "tlou_resident_hit_cache.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

int main() {
    using VideoCore::Detail::ResidentHitCache;
    constexpr std::size_t Blocks = 8192;
    std::array<unsigned char, Blocks> resident{};
    ResidentHitCache cache;
    std::mt19937_64 rng{0x20261110};
    std::uint64_t hits = 0, slow = 0;
    auto baseline = [&](unsigned first, unsigned last) {
        for (unsigned i = first; i <= last; ++i) if (!resident[i]) return false;
        return true;
    };
    for (unsigned n = 0; n < 2000000; ++n) {
        const unsigned first = static_cast<unsigned>(rng() % (Blocks - 16));
        const unsigned last = first + static_cast<unsigned>(rng() % 16);
        if (n % 53 == 0) {
            // Releases invalidate ALL hits. Subsequent allocations are never
            // assumed resident until the true set confirms them again.
            for (unsigned i = first; i <= last; ++i) resident[i] = 0;
            cache.Invalidate();
        } else if (n % 7 == 0) {
            for (unsigned i = first; i <= last; ++i) resident[i] = 1;
        }
        const bool authoritative = baseline(first, last);
        const bool fast = cache.Hit(first, last);
        if (fast) {
            assert(authoritative);
            ++hits;
        } else {
            ++slow;
            if (authoritative) cache.Remember(first, last);
        }
        assert(cache.Hit(first, last) == authoritative);
    }
    constexpr unsigned Keys = 64, Repeats = 2500000;
    std::array<std::pair<unsigned, unsigned>, Keys> queries{};
    for (unsigned i = 0; i < Keys; ++i) {
        queries[i] = {i * 37, i * 37 + 3};
        for (unsigned k = queries[i].first; k <= queries[i].second; ++k) resident[k] = 1;
    }
    cache.Invalidate();
    for (auto [first, last] : queries) {
        assert(baseline(first, last));
        cache.Remember(first, last);
    }
    auto bench = [&](bool use_cache) {
        std::uint64_t sum = 0;
        auto start = std::chrono::steady_clock::now();
        for (unsigned i = 0; i < Repeats; ++i) {
            const auto [first, last] = queries[(i * 73ull + (i / 41)) % Keys];
            const bool cached = use_cache && cache.Hit(first, last);
            const bool present = cached || baseline(first, last);
            if (use_cache && !cached && present) cache.Remember(first, last);
            sum += present;
        }
        const auto end = std::chrono::steady_clock::now();
        return std::pair<double, std::uint64_t>{
            std::chrono::duration<double, std::nano>(end - start).count() / Repeats, sum};
    };
    const auto old = bench(false);
    const auto opt = bench(true);
    assert(old.second == opt.second);
    std::printf("TLOU resident memo: 2M mutation/query parity PASS; hit=%llu fallback=%llu; "
                "2.5M hot queries baseline=%.2fns cache=%.2fns ratio=%.3f (host only)\n",
                (unsigned long long)hits, (unsigned long long)slow,
                old.first, opt.first, opt.first / old.first);
}
