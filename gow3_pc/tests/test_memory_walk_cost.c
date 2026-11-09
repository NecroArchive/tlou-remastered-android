/* Exercise the real runtime implementations, without mapping guest addresses. */
#include "../src/runtime_memory.c"
#include <assert.h>
static int32_t test_errno;
int32_t *runtime_errno(void) { return &test_errno; }
uintptr_t runtime_lookup(const RuntimeExport *table,size_t count,const char *name) {
    (void)table; (void)count; (void)name; return 0;
}
#ifdef TEST_VMA_LOOKUP_COUNTS
static uint64_t lookup_count;
void __cyg_profile_func_enter(void *,void *) __attribute__((no_instrument_function));
void __cyg_profile_func_exit(void *,void *) __attribute__((no_instrument_function));
void __cyg_profile_func_enter(void *fn,void *caller) {
    (void)caller; if(fn==(void *)vma_index) ++lookup_count;
}
void __cyg_profile_func_exit(void *fn,void *caller) { (void)fn; (void)caller; }
#endif
/* Frozen v0.30 reference: keep independent of the optimized table walk. */
static void reference_copy(uintptr_t address, void *dest, uint64_t size) {
    unsigned char *out=dest;
    { const void *fast=cached_backing(address,size); if (fast) { memcpy(dest,fast,size); return; } }
    read_lock();
    { size_t first=vma_index(address); if (first<vma_count && vmas[first].start<=address) cache_region(first); }
    for (uintptr_t at=address, end=address+size; at<end;) {
        size_t i=vma_index(at);
        uintptr_t next=end;
        if (i<vma_count && vmas[i].start<=at) {
            next=vmas[i].end<end ? vmas[i].end : end;
            if (vmas[i].kind==KIND_RESERVED) memset(out+(at-address),0,next-at);
            else if (backing_base) memcpy(out+(at-address),backing_base+vmas[i].phys+(at-vmas[i].start),next-at);
            else memcpy(out+(at-address),(const void *)at,next-at);
        } else {
            if (i<vma_count && vmas[i].start<end) next=vmas[i].start;
            memset(out+(at-address),0,next-at);
        }
        at=next;
    }
    read_unlock();
}
static uint64_t reference_clamp(uintptr_t address, uint64_t size) {
    const CachedRegion *c=(runtime_disabled_optimizations & 1) ? NULL : cached_region(address);
    if (c && c->kind!=KIND_RESERVED && size<=c->end-address) return size;
    read_lock();
    { size_t i=vma_index(address); if (i<vma_count && vmas[i].start<=address) cache_region(i); }
    uint64_t length=0;
    for (size_t i=vma_index(address); i<vma_count && length<size; ++i) {
        if (vmas[i].start>address+length || vmas[i].kind==KIND_RESERVED) break;
        length=vmas[i].end-address;
    }
    read_unlock();
    return length<size ? length : size;
}

/* Independent original 8-slot scan, to test the hot region hint
 * against every cache generation and mapped/partially unmapped range. */
static const CachedRegion *reference_cached_region(uintptr_t address) {
    uint64_t generation=__atomic_load_n(&table_generation,__ATOMIC_ACQUIRE);
    if(generation&1)return NULL;
    for(unsigned i=0;i<8;++i){
        const CachedRegion *c=&region_cache[i];
        if(c->generation==generation && c->end && c->start<=address && address<c->end)return c;
    }
    return NULL;
}
static void check_region_hint(uintptr_t address) {
    const CachedRegion *a=reference_cached_region(address),*b=cached_region(address);
    assert((a!=NULL)==(b!=NULL));
    if(a&&b) {
        assert(a->start==b->start && a->end==b->end && a->kind==b->kind && a->phys==b->phys);
    }
}
static uint64_t rng=0xd48b36aa19ULL;
static uint64_t random_value(void) { rng^=rng<<13; rng^=rng>>7; rng^=rng<<17; return rng; }
static void cold_cache(void) {
    memset(region_cache,0,sizeof(region_cache));
    region_cache_next=0;
    region_cache_hot=0;
}
static void setup(void) {
    tlou_memory_walk_once=1;
    vma_count=vma_capacity=512;
    vmas=calloc(vma_count,sizeof(*vmas));
    backing_base=malloc(vma_count*PAGE);
    assert(vmas && backing_base);
    for(size_t i=0;i<vma_count*PAGE;++i) backing_base[i]=(unsigned char)(i*37+i/131);
    uintptr_t at=USER_MIN+PAGE;
    for(size_t i=0;i<vma_count;++i) {
        vmas[i]=(Vma){at,at+PAGE,i%11==3?KIND_RESERVED:KIND_DIRECT,3,0,(i%67)*PAGE,{0}};
        at+=PAGE+(i%5==0?PAGE:0);
    }
    table_generation=2;
}
static void correctness(void) {
    enum { MAX_COPY=8*16384 };
    unsigned char *a=malloc(MAX_COPY+32), *b=malloc(MAX_COPY+32);
    assert(a && b);
    for(unsigned i=0;i<50000;++i) {
        uintptr_t at=USER_MIN+random_value()%(vmas[vma_count-1].end-USER_MIN+PAGE);
        uint64_t size=random_value()%(MAX_COPY+1);
        check_region_hint(at);
        cold_cache(); uint64_t expected=reference_clamp(at,size);
        cold_cache(); assert(runtime_memory_clamp(at,size)==expected);
        /* Exercise a generation-valid cache as well as misses, holes and reservations. */
        assert(runtime_memory_clamp(at,size)==expected);
        check_region_hint(at);
        /* For a read-only ring buffer the only decision is whether the
         * contiguous mapped extent is <=16KiB; if so copy exactly that
         * extent. A clamp capped at threshold+1 has the same result. */
        const uint64_t cap=size<(16u*1024+1u)?size:(16u*1024+1u);
        const uint64_t capped=runtime_memory_clamp(at,cap);
        assert((expected>16u*1024 && capped>16u*1024) ||
               (expected<=16u*1024 && capped==expected));
        memset(a,0xa5,MAX_COPY+32); memset(b,0xa5,MAX_COPY+32);
        cold_cache(); reference_copy(at,a+16,size);
        cold_cache(); runtime_memory_copy_backing(at,b+16,size);
        assert(!memcmp(a,b,MAX_COPY+32));
        runtime_memory_copy_backing(at,b+16,size);
        assert(!memcmp(a,b,MAX_COPY+32));
        memset(a,0xa5,MAX_COPY+32); memset(b,0xa5,MAX_COPY+32);
        cold_cache(); uint64_t clipped=reference_clamp(at,size);
        if(clipped) reference_copy(at,a+16,clipped);
        cold_cache(); assert(runtime_memory_clamp(at,size)==clipped);
        if(clipped) runtime_memory_copy_backing(at,b+16,clipped);
        assert(!memcmp(a,b,MAX_COPY+32));
        if(i%127==0) {
            /* Real write-lock generation invalidation, including alias and reservation changes. */
            size_t index=random_value()%vma_count;
            write_lock();
            vmas[index].phys=(random_value()%67)*PAGE;
            vmas[index].kind=vmas[index].kind==KIND_RESERVED?KIND_DIRECT:KIND_RESERVED;
            write_unlock();
        }
    }
    /* Verify interleaved hot-region reads where last USED differs from last INSERTED,
     * and a generation change that invalidates every cached pointer. */
    cold_cache();
    for (unsigned i=0;i<8;++i) (void)runtime_memory_clamp(vmas[i].start+64,128);
    for (unsigned i=0;i<4000;++i) {
        uintptr_t address=vmas[(i/2)%8].start+64;
        check_region_hint(address);
        (void)cached_region(address);
        check_region_hint(address);
    }
    write_lock();
    vmas[1].phys=(vmas[1].phys+PAGE)%(67*PAGE);
    write_unlock();
    for (unsigned i=0;i<8;++i) check_region_hint(vmas[i].start+64);
    free(a); free(b);
    puts("Memory walks: 50000 reference comparisons passed (gaps, reservations, aliases, zero size, cache and generation changes)");
}
static volatile uint64_t result_sink;
static uint64_t now_ns(void) {
    struct timespec t; assert(!clock_gettime(CLOCK_MONOTONIC,&t));
    return (uint64_t)t.tv_sec*1000000000+t.tv_nsec;
}
static int compare_double(const void *a,const void *b) {
    double x=*(const double *)a,y=*(const double *)b; return (x>y)-(x<y);
}
/* Function pointers preserve the public call boundary for both versions. */
typedef uint64_t (*Clamp)(uintptr_t,uint64_t);
typedef void (*Copy)(uintptr_t,void *,uint64_t);
/* Native cache-last validation uses the production function and the
 * exact old 8-way scan with the same TLS table and generation. */
typedef const CachedRegion *(*RegionLookup)(uintptr_t);
static __attribute__((noinline)) double run_region_lookup(RegionLookup lookup,unsigned pattern) {
    uint64_t sum=0;
    cold_cache();
    for(unsigned i=0;i<8;++i) {
        (void)runtime_memory_clamp(vmas[i].start+64,128);
    }
    const uintptr_t last=vmas[7].start+64;
    const uint64_t begin=now_ns();
    for(unsigned i=0;i<1000000;++i) {
        const uintptr_t at=pattern==0?last:
                           pattern==1?vmas[i%8].start+64:
                           pattern==2?(i%10<7?last:vmas[i%8].start+64):
                                      vmas[(i/2)%8].start+64;
        const CachedRegion *c=lookup(at);
        sum+=c?c->phys:0;
    }
    result_sink=sum;
    return (double)(now_ns()-begin)/1000000.0;
}
static void benchmark_region_hint(void) {
    const char *names[]={"last-inserted","rotating-eight","last-70pct","paired-eight"};
    for(unsigned p=0;p<4;++p) {
        double old[9],now[9];
        for(unsigned i=0;i<9;++i) {
            if(i%2) {now[i]=run_region_lookup(cached_region,p);
                     old[i]=run_region_lookup(reference_cached_region,p);}
            else {old[i]=run_region_lookup(reference_cached_region,p);
                  now[i]=run_region_lookup(cached_region,p);}
        }
        qsort(old,9,sizeof(double),compare_double);
        qsort(now,9,sizeof(double),compare_double);
        printf("cached-region-hint %-18s old_ns=%.2f new_ns=%.2f ratio=%.3f\n",
               names[p],old[4],now[4],now[4]/old[4]);
    }
}
static __attribute__((noinline)) double run_clamp(Clamp fn,unsigned workload) {
    uint64_t sum=0; cold_cache();
    uint64_t begin=now_ns();
    for(unsigned j=0;j<400000;++j) {
        size_t i=workload==0?1:((j*17)%512);
        sum+=fn(vmas[i].start+64,workload==2?PAGE*3:128);
    }
    uint64_t end=now_ns(); result_sink=sum; return (double)(end-begin)/400000;
}
static __attribute__((noinline)) double run_copy(Copy fn,unsigned workload) {
    unsigned char out[4*16384]; uint64_t sum=0; cold_cache();
    uint64_t begin=now_ns();
    for(unsigned j=0;j<100000;++j) {
        size_t i=workload==0?1:((j*17)%512);
        fn(vmas[i].start+64,out,workload==2?PAGE*3:128); sum+=out[j%128];
    }
    uint64_t end=now_ns(); result_sink=sum; return (double)(end-begin)/100000;
}
/* CollectRingBindings clamps before CopySparseMemory: the clamp warms the same
 * thread's backing cache. Do not add independently cold clamp/copy savings. */
/* Every valid shader buffer used to clamp twice: first to reject garbage
 * descriptors and then to obtain the same extent for Vulkan binding.
 * Model that exact hot path with the real runtime_memory_clamp, including
 * cache-hot and rotating VMA layouts. Both versions must produce equal ranges.
 */
static __attribute__((noinline)) double run_shader_buffer_clamp(int repeated,unsigned workload) {
    uint64_t sum=0; cold_cache();
    const uint64_t begin=now_ns();
    for(unsigned j=0;j<400000;++j) {
        size_t i=workload==0?1:((j*17)%512);
        uintptr_t address=vmas[i].start+64;
        uint64_t requested=workload==2?PAGE*3:128;
        uint64_t mapped=runtime_memory_clamp(address,requested);
        /* Exactly the second clamp that BindBuffers removed. */
        uint64_t bound=repeated?runtime_memory_clamp(address,requested):mapped;
        assert(bound==mapped);
        sum+=bound;
    }
    result_sink=sum;
    return (double)(now_ns()-begin)/400000;
}
static void benchmark_shader_buffer_clamp(void) {
    const char *names[]={"cache-hot-128B","rotating-512-128B","cross-regions-48KiB"};
    for(unsigned w=0;w<3;++w) {
        double old[9],next[9];
        for(unsigned i=0;i<9;++i) {
            if(i%2) { next[i]=run_shader_buffer_clamp(0,w); old[i]=run_shader_buffer_clamp(1,w); }
            else    { old[i]=run_shader_buffer_clamp(1,w); next[i]=run_shader_buffer_clamp(0,w); }
        }
        qsort(old,9,sizeof(double),compare_double); qsort(next,9,sizeof(double),compare_double);
        printf("shader-buffer-clamp %-22s two_ns=%.2f one_ns=%.2f saved_ns/bind=%.2f reduction=%.1f%%\n",
               names[w],old[4],next[4],old[4]-next[4],
               100*(old[4]-next[4])/old[4]);
    }
}
static __attribute__((noinline)) double run_clamp_then_copy(Clamp clamp,Copy copy,unsigned workload) {
    unsigned char out[4*16384]; uint64_t sum=0; cold_cache();
    uint64_t begin=now_ns();
    for(unsigned j=0;j<100000;++j) {
        size_t i=workload==0?1:((j*17)%512);
        uintptr_t at=vmas[i].start+64;
        uint64_t size=clamp(at,workload==2?PAGE*3:128);
        if(size) { copy(at,out,size); sum+=out[0]; }
    }
    uint64_t end=now_ns(); result_sink=sum; return (double)(end-begin)/100000;
}
static void benchmark(void) {
    const char *names[]={"cache-hot-128B","rotating-512-128B","cross-regions-48KiB"};
    for(unsigned kind=0;kind<3;++kind) for(unsigned w=0;w<3;++w) {
        double old[9],next[9];
        for(unsigned repeat=0;repeat<10;++repeat) {
            double a,b;
            if(repeat%2) {
                b=kind==2?run_clamp_then_copy(runtime_memory_clamp,runtime_memory_copy_backing,w):kind?run_copy(runtime_memory_copy_backing,w):run_clamp(runtime_memory_clamp,w);
                a=kind==2?run_clamp_then_copy(reference_clamp,reference_copy,w):kind?run_copy(reference_copy,w):run_clamp(reference_clamp,w);
            } else {
                a=kind==2?run_clamp_then_copy(reference_clamp,reference_copy,w):kind?run_copy(reference_copy,w):run_clamp(reference_clamp,w);
                b=kind==2?run_clamp_then_copy(runtime_memory_clamp,runtime_memory_copy_backing,w):kind?run_copy(runtime_memory_copy_backing,w):run_clamp(runtime_memory_clamp,w);
            }
            if(repeat) { old[repeat-1]=a; next[repeat-1]=b; }
        }
        qsort(old,9,sizeof(double),compare_double); qsort(next,9,sizeof(double),compare_double);
        printf("%s %-23s old_ns=%.2f new_ns=%.2f saved_ns/call=%.2f reduction=%.1f%%\n",kind==2?"clamp+copy":kind?"copy":"clamp",names[w],old[4],next[4],old[4]-next[4],100*(old[4]-next[4])/old[4]);
    }
}
static void count_work(void) {
#ifdef TEST_VMA_LOOKUP_COUNTS
    unsigned char out[4*16384];
    cold_cache(); lookup_count=0; reference_clamp(vmas[1].start,128); assert(lookup_count==2);
    cold_cache(); lookup_count=0; runtime_memory_clamp(vmas[1].start,128); assert(lookup_count==1);
    lookup_count=0; runtime_memory_clamp(vmas[1].start,128); assert(lookup_count==0);
    cold_cache(); lookup_count=0; reference_copy(vmas[1].start,out,3*PAGE); uint64_t old=lookup_count;
    cold_cache(); lookup_count=0; runtime_memory_copy_backing(vmas[1].start,out,3*PAGE);
    assert(old==4 && lookup_count==1);
    cold_cache(); lookup_count=0;
    reference_clamp(vmas[1].start,128); reference_copy(vmas[1].start,out,128);
    assert(lookup_count==2);
    cold_cache(); lookup_count=0;
    runtime_memory_clamp(vmas[1].start,128); runtime_memory_copy_backing(vmas[1].start,out,128);
    assert(lookup_count==1);
    puts("Lookup work gate passed: clamp miss 2->1, hot 0->0, three-region copy 4->1 binary searches");
    puts("Sequence work gate passed: clamp+128B copy 2->1 binary searches total, NOT 4->2; copy already cache-hot after clamp");
#endif
}
int main(int argc,char **argv) {
    setup(); count_work(); correctness();
    tlou_memory_walk_once=0; correctness(); tlou_memory_walk_once=1;
    if(argc<2 || strcmp(argv[1],"--correctness-only")) {
        benchmark();
        benchmark_shader_buffer_clamp();
        benchmark_region_hint();
    }
    free(vmas); free(backing_base); return 0;
}
