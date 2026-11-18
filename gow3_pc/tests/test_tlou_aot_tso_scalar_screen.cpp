// SPDX-License-Identifier: GPL-2.0-or-later
// ARM64 synthetic screen ONLY. Do not infer x86-TSO correctness or FPS.
// Source conservative lowering: android/aot/patch_recompiler_tso.py.
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>
#if !defined(__aarch64__)
#error "ARM64-only: x86 measurements are misleading"
#endif
using Word = std::uint64_t;
static constexpr unsigned Iterations = 1200000;
__attribute__((noinline)) static Word ConservativeLoad(const Word* p) {
    asm volatile("dmb ish" ::: "memory");
    const Word value = *reinterpret_cast<const volatile Word*>(p);
    asm volatile("dmb ish" ::: "memory");
    return value;
}
__attribute__((noinline)) static void ConservativeStore(Word* p, Word value) {
    asm volatile("dmb ish" ::: "memory");
    *reinterpret_cast<volatile Word*>(p) = value;
    asm volatile("dmb ish" ::: "memory");
}
// Atomic candidate is ONLY legal for adequately aligned scalar guest addresses.
// SIMD, unaligned, and split-cache-line addresses need a validated fallback.
__attribute__((noinline)) static Word AtomicAcquireLoad(const Word* p) {
    return __atomic_load_n(p, __ATOMIC_ACQUIRE);
}
__attribute__((noinline)) static void AtomicReleaseStore(Word* p, Word value) {
    __atomic_store_n(p, value, __ATOMIC_RELEASE);
}
using Load = Word(*)(const Word*);
using Store = void(*)(Word*,Word);
static double Run(Load load,Store store) {
    alignas(64) std::array<Word,4> cells{8,20,0,0};
    Word acc=0;
    const auto t0=std::chrono::steady_clock::now();
    for(unsigned i=0;i<Iterations;++i) {
        const unsigned slot=i&1u;
        const Word v=load(&cells[slot]);
        store(&cells[slot+2],v+(i&7u));
        acc+=v;
    }
    const auto t1=std::chrono::steady_clock::now();
    assert(cells[2]==8+((Iterations-2)&7u));
    assert(cells[3]==20+((Iterations-1)&7u));
    assert(acc==(Iterations/2ull)*28);
    return std::chrono::duration<double,std::nano>(t1-t0).count()/Iterations;
}
int main() {
    std::array<std::vector<double>,2> results;
    for(unsigned round=0;round<24;++round) for(unsigned pass=0;pass<2;++pass) {
        const unsigned mode=pass^(round&1u);
        const double ns=mode==0 ? Run(ConservativeLoad,ConservativeStore)
                                : Run(AtomicAcquireLoad,AtomicReleaseStore);
        if(round>3)results[mode].push_back(ns);
    }
    for(auto& values:results)std::sort(values.begin(),values.end());
    const double before=results[0][results[0].size()/2];
    const double after=results[1][results[1].size()/2];
    std::printf("TLOU AOT TSO SCALAR SCREEN ARM64: full-fence=%.2f ns/pair acquire-release=%.2f ns/pair ratio=%.3f (aligned scalar synthetic ONLY; NOT gameplay proof)\n",
                before,after,after/before);
}
