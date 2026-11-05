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

static uint64_t rng=0xd48b36aa19ULL;
static uint64_t random_value(void) { rng^=rng<<13; rng^=rng>>7; rng^=rng<<17; return rng; }
static void cold_cache(void) { memset(region_cache,0,sizeof(region_cache)); region_cache_next=0; }
static void setup(void) {
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
        cold_cache(); uint64_t expected=reference_clamp(at,size);
        cold_cache(); assert(runtime_memory_clamp(at,size)==expected);
        /* Exercise a generation-valid cache as well as misses, holes and reservations. */
        assert(runtime_memory_clamp(at,size)==expected);
        memset(a,0xa5,MAX_COPY+32); memset(b,0xa5,MAX_COPY+32);
        cold_cache(); reference_copy(at,a+16,size);
        cold_cache(); runtime_memory_copy_backing(at,b+16,size);
        assert(!memcmp(a,b,MAX_COPY+32));
        runtime_memory_copy_backing(at,b+16,size);
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
static void benchmark(void) {
    const char *names[]={"cache-hot-128B","rotating-512-128B","cross-regions-48KiB"};
    for(unsigned kind=0;kind<2;++kind) for(unsigned w=0;w<3;++w) {
        double old[9],next[9];
        for(unsigned repeat=0;repeat<10;++repeat) {
            double a,b;
            if(repeat%2) {
                b=kind?run_copy(runtime_memory_copy_backing,w):run_clamp(runtime_memory_clamp,w);
                a=kind?run_copy(reference_copy,w):run_clamp(reference_clamp,w);
            } else {
                a=kind?run_copy(reference_copy,w):run_clamp(reference_clamp,w);
                b=kind?run_copy(runtime_memory_copy_backing,w):run_clamp(runtime_memory_clamp,w);
            }
            if(repeat) { old[repeat-1]=a; next[repeat-1]=b; }
        }
        qsort(old,9,sizeof(double),compare_double); qsort(next,9,sizeof(double),compare_double);
        printf("%s %-23s old_ns=%.2f new_ns=%.2f reduction=%.1f%%\n",kind?"copy":"clamp",names[w],old[4],next[4],100*(old[4]-next[4])/old[4]);
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
    puts("Lookup work gate passed: clamp miss 2->1, hot 0->0, three-region copy 4->1 binary searches");
#endif
}
int main(int argc,char **argv) {
    setup(); count_work(); correctness();
    if(argc<2 || strcmp(argv[1],"--correctness-only")) benchmark();
    free(vmas); free(backing_base); return 0;
}
