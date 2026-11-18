// SPDX-License-Identifier: MIT
// Verify the AOT string helper against real x86-64 SSE4.2 instructions.
// Controls 0-63 × 10,000 randomized vectors = 640,000 comparisons.
// VPCMPISTRX is FEX IR's intermediate result: mask in low 16 bits and
// NZCV in bits 31-28. We use PCMPISTRM (bit 6=0) to measure this mask
// on the original ISA and PCMPISTRx status intrinsics to compare flags.
#include <immintrin.h>
#include <cstdint>
#include <cstdio>
#include <random>

extern "C" uint32_t bbr_vpcmpistrx(uint64_t,uint64_t,uint64_t,uint64_t,uint32_t);

template<int C>
uint32_t x86(__m128i a,__m128i b,int control) {
    if constexpr(C>=64) return 0;
    else {
        if(control==C) {
            const __m128i m=_mm_cmpistrm(a,b,C);
            const uint32_t mask=uint16_t(_mm_cvtsi128_si32(m));
            const uint32_t flags =
                (_mm_cmpistrs(a,b,C)?8u:0u) |
                (_mm_cmpistrz(a,b,C)?4u:0u) |
                (_mm_cmpistrc(a,b,C)?2u:0u) |
                (_mm_cmpistro(a,b,C)?1u:0u);
            return mask | (flags<<28);
        }
        return x86<C+1>(a,b,control);
    }
}

int main() {
    #if defined(__x86_64__)
    if(!__builtin_cpu_supports("sse4.2")) {
        std::puts("SKIP: no SSE4.2 CPU support");
        return 0;
    }
    #endif
    std::mt19937_64 rng(3233);
    unsigned failures=0;
    for(int ctl=0;ctl<64;ctl++)
        for(int i=0;i<10000;i++) {
            uint64_t a0=rng(), a1=rng(), b0=rng(), b1=rng();
            if(i%11==0)a0 &= 0xffffffff00ffffull;
            if(i%13==0)b0 &= 0xffffffff0000ffffull;
            __m128i a=_mm_set_epi64x(a1,a0), b=_mm_set_epi64x(b1,b0);
            const uint32_t expected=x86<0>(a,b,ctl);
            const uint32_t actual=bbr_vpcmpistrx(a0,a1,b0,b1,ctl);
            if(expected!=actual) {
                if(failures<12)
                    std::printf("Mismatch: control=%d case=%d expected=%08x actual=%08x\n",
                                ctl,i,expected,actual);
                ++failures;
            }
        }
    std::printf("VPCMPISTRX: %u failures / 640000 x86 hardware tests\n",failures);
    return failures==0 ? 0 : 1;
}
