// SPDX-License-Identifier: GPL-2.0-or-later
// Benchmark the EXACT runtime_memory_clamp() implementation, not a replica.
// Include the C translation unit to inject synthetic guest VMAs without
// creating physical mappings. Other sections are discarded at link time.
#define _GNU_SOURCE
#include "../src/runtime_memory.c"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static volatile uint64_t escape_sum;
static uint64_t mono_bench_ns(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC_RAW,&t);
    return (uint64_t)t.tv_sec*1000000000ull+(uint64_t)t.tv_nsec;
}
static void vm_setup(void) {
    const size_t n=1024;
    vmas=calloc(n,sizeof(*vmas));
    assert(vmas);
    vma_capacity=vma_count=n;
    for(size_t i=0;i<n;++i){
        vmas[i].start=USER_MIN+(uintptr_t)i*0x100000;
        vmas[i].end=vmas[i].start+0x100000;
        vmas[i].kind=KIND_DIRECT;
        vmas[i].prot=3;
    }
    table_generation=2;
}
static void Verify(void) {
    vm_setup();
    // Complete, partial and cross-region clamps exercise both the region
    // cache and the binary-search/lock fallback paths.
    for(unsigned i=0;i<1024;++i) {
        const uintptr_t addr=vmas[i].start+256;
        assert(runtime_memory_clamp(addr,4096)==4096);
        assert(runtime_memory_clamp(addr,0xFFFE0)==0xFFFE0);
        const uint64_t max_available=(1024ull-i)*0x100000ull-256;
        const uint64_t requested=0x1FFFFFull;
        assert(runtime_memory_clamp(addr,requested)==(requested<max_available?requested:max_available));
    }
    assert(runtime_memory_clamp(vmas[1023].end+0x1000,4096)==0);
}
static double Trial(unsigned reads_per_bind, unsigned pattern, unsigned use_cache) {
    runtime_disabled_optimizations = use_cache ? 0 : 1;
    const unsigned n=500000;
    uint64_t sum=0;
    const uint64_t start=mono_bench_ns();
    for(unsigned i=0;i<n;++i) {
        uintptr_t addr;
        if(pattern==0) {
            addr=vmas[100].start+128; // consistently hot TLS cache
        } else {
            const unsigned index=(i*1193u)%1024;
            addr=vmas[index].start+(i&1023u);
        }
        const uint64_t size=4096;
        const uint64_t mapped=runtime_memory_clamp(addr,size);
        sum+=mapped;
        if(reads_per_bind==2) {
            const uint64_t same=runtime_memory_clamp(addr,size);
            if(mapped!=same) abort();
            sum+=same;
        }
        __asm__ __volatile__("" ::: "memory");
    }
    escape_sum=sum;
    return (double)(mono_bench_ns()-start)/n;
}
static double Median(unsigned reads,unsigned pattern,unsigned use_cache) {
    double ms[9];
    for(unsigned i=0;i<9;++i) ms[i]=Trial(reads,pattern,use_cache);
    for(unsigned i=0;i<9;++i)for(unsigned j=i+1;j<9;++j)
        if(ms[j]<ms[i]){const double tmp=ms[i];ms[i]=ms[j];ms[j]=tmp;}
    return ms[4];
}
int main(int argc,char** argv) {
    Verify();
    if(argc>1 && strcmp(argv[1],"--correctness-only")==0){
        puts("Exact runtime_memory_clamp VMA coverage: PASS");return 0;
    }
    for(unsigned cache=0;cache<=1;++cache) {
        for(unsigned pattern=0;pattern<2;++pattern) {
            Trial(2,pattern,cache);Trial(1,pattern,cache);
            const double original=Median(2,pattern,cache);
            const double optimized=Median(1,pattern,cache);
            const double saved=original-optimized;
            printf("real-runtime-memory-clamp cache=%s vma=%s original=%.2f optimized=%.2f saved=%.2f ns/bind\\n",
                   cache?"enabled":"disabled",pattern?"1024-region scan":"single hot region",
                   original,optimized,saved);
            printf("at 1580 dispatch/frame and 8 bindings/dispatch: %.3f ms/frame MAX if every binding repeats clamp\\n",
                   1580.0*8*saved/1e6);
        }
    }
    free(vmas);
    return 0;
}
