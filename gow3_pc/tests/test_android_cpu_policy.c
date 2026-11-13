#define _GNU_SOURCE
#include "../src/android_cpu_policy.h"
#include <assert.h>
#include <sys/wait.h>

static void expect(const TlCpuRank *r,size_t count,unsigned long long bits,int metric) {
    cpu_set_t selected;assert(tlou_fast_cpu_set(r,count,&selected)==metric);
    unsigned n=0;
    for (unsigned cpu=0;cpu<CPU_SETSIZE;++cpu) {
        int expected=cpu<64 && (bits & (1ull<<cpu));
        assert(!!CPU_ISSET(cpu,&selected)==!!expected);
        n+=!!expected;
    }
    assert((unsigned)CPU_COUNT(&selected)==n);
}
static void *restore_worker(void *data) {
    const cpu_set_t *original=data;cpu_set_t restored;
    tlou_cpu_policy_apply(NULL,0);
    return (void *)(uintptr_t)(sched_getaffinity(0,sizeof(restored),&restored) || memcmp(&restored,original,sizeof(restored)));
}
int main(void) {
    TlCpuRank elite[8];
    for (unsigned i=0;i<8;++i) elite[i]=(TlCpuRank){i,i<6 ? 800:1024,i<6 ? 3532000:4320000};
    expect(elite,8,0xc0,1);expect(elite,6,0x3f,0); // restricted launch mask
    for (unsigned i=0;i<8;++i) elite[i].capacity=1024;
    expect(elite,8,0xc0,2); // equal capacities: frequency discriminates the two groups
    TlCpuRank three_tier[]={{0,300,1000},{2,300,1000},{4,700,2000},{5,700,2000},{7,1024,3000}};
    expect(three_tier,5,0xb0,1); // single prime includes the next tier, never just one core
    TlCpuRank unknown[]={{1,0,0},{3,0,0},{6,0,0}};
    expect(unknown,3,0x4a,0);
    TlCpuRank partial[]={{2,0,1500},{6,1024,0}};
    expect(partial,2,0x44,0); // incomplete metadata must not exclude an unranked CPU
    // AOT-only shared prime mask: both critical threads can migrate to
    // CPU6 or CPU7. Exclusive CPU6 pinning regressed FPS when other threads
    // still had CPU time to spare. FEX and all six workers remain unrestricted.
    cpu_set_t primes,for_gpu,for_draw;
    CPU_ZERO(&primes);CPU_SET(6,&primes);CPU_SET(7,&primes);
    assert(tlou_aot_split_prime_mask("GPU command",&primes,&for_gpu));
    assert(tlou_aot_split_prime_mask("draw recording",&primes,&for_draw));
    assert(CPU_COUNT(&for_gpu)==2 && CPU_ISSET(7,&for_gpu) && CPU_ISSET(6,&for_gpu));
    assert(CPU_COUNT(&for_draw)==2 && CPU_ISSET(7,&for_draw) && CPU_ISSET(6,&for_draw));
    assert(!tlou_aot_split_prime_mask("guest main",&primes,&for_gpu));
    assert(!tlou_aot_split_prime_mask("NdJobWorkerThread",&primes,&for_gpu));
    assert(!tlou_aot_split_prime_mask(NULL,&primes,&for_gpu));
    cpu_set_t only_one;CPU_ZERO(&only_one);CPU_SET(6,&only_one);
    assert(!tlou_aot_split_prime_mask("GPU command",&only_one,&for_gpu));
    assert(!tlou_aot_split_prime_enabled());
    assert(!setenv("TLOU_AOT_SPLIT_PRIME","1",1));
    assert(!tlou_aot_split_prime_enabled()); // FEX mode remains untouched
    assert(!setenv("BB_AOT","/data/local/libtlouaot.so",1));
    assert(tlou_aot_split_prime_enabled());
    assert(!unsetenv("BB_AOT"));assert(!unsetenv("TLOU_AOT_SPLIT_PRIME"));
    cpu_set_t set,roundtrip;char text[CPU_SETSIZE*6];
    CPU_ZERO(&set);CPU_SET(2,&set);CPU_SET(CPU_SETSIZE-1,&set);
    assert(tlou_cpu_mask_text(&set,text,sizeof(text)));
    assert(tlou_cpu_parse_mask(text,&roundtrip));assert(!memcmp(&set,&roundtrip,sizeof(set)));
    const char *invalid[]={"","-1","1,","1,,2","2x","999999999999999999999999999"};
    for (unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);++i) assert(!tlou_cpu_parse_mask(invalid[i],&set));
    // Own-process smoke test: a narrowed critical parent must not narrow guest workers.
    cpu_set_t original;assert(!sched_getaffinity(0,sizeof(original),&original));
    assert(tlou_cpu_mask_text(&original,text,sizeof(text)));
    assert(!setenv("TLOU_FAST_RENDER_CORES","1",1));assert(!setenv("TLOU_BASE_CPU_SET",text,1));
    int first=-1;for (int i=0;i<CPU_SETSIZE;++i) if (CPU_ISSET(i,&original)) { first=i;break; }
    assert(first>=0);char one[32];snprintf(one,sizeof(one),"%d",first);assert(!setenv("TLOU_FAST_CPU_SET",one,1));
    tlou_cpu_policy_apply(NULL,1);
    cpu_set_t actual;assert(!sched_getaffinity(0,sizeof(actual),&actual));
    // If a sandbox denies affinity changes, the original mask must remain intact.
    if (CPU_COUNT(&actual)==1) {
        pthread_t thread;void *result=(void *)1;
        assert(!pthread_create(&thread,NULL,restore_worker,&original));
        assert(!pthread_join(thread,&result));assert(!result);
        assert(!sched_getaffinity(0,sizeof(actual),&actual));assert(CPU_COUNT(&actual)==1);

    } else assert(!memcmp(&actual,&original,sizeof(original)));
    tlou_cpu_policy_apply(NULL,0);assert(!sched_getaffinity(0,sizeof(actual),&actual));assert(!memcmp(&actual,&original,sizeof(actual)));
    assert(!setenv("TLOU_FAST_RENDER_CORES","0",1));tlou_cpu_policy_apply(NULL,1);
    assert(!sched_getaffinity(0,sizeof(actual),&actual));assert(!memcmp(&actual,&original,sizeof(actual)));
    puts("android-cpu-policy: topology, restricted mask, missing metadata, inheritance, restore and OFF passed");
}
