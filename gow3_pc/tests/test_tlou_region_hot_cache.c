/* SPDX-License-Identifier: GPL-2.0-or-later
 * Compare the original 8-entry VMA lookup with the native hot page-bucket
 * lookup using the exact production slot and generation/bounds predicates.
 * No cached entry may survive a remap (VM generation advance), including a
 * same-page VMA split, reserved interval, or address outside the cached VMA. */
#include "tlou_region_hot_lookup.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef struct {uint64_t generation; uintptr_t start,end;int kind;uint64_t phys;} Region;
typedef struct {
    Region recent[8],hot[TLOU_HOT_REGION_SLOTS];
    unsigned next;
    uint64_t fallback,hot_hits, recent_hits;
} Lookup;
enum {VMAS=4096};
static Region table[VMAS];
static volatile uint64_t checksum;
static uint64_t mix=0xdeadbeeff00da11ULL;
static uint32_t rnd(void) {mix^=mix<<13;mix^=mix>>7;mix^=mix<<17;return (uint32_t)(mix>>16);}
static Region* expected(uintptr_t addr) {
    size_t lo=0,hi=VMAS;
    while(lo<hi) {
        size_t md=(lo+hi)/2;
        if(table[md].end<=addr)lo=md+1;
        else hi=md;
    }
    if(lo<VMAS && table[lo].start<=addr)return &table[lo];
    return NULL;
}
static const Region* lookup(Lookup* c,uintptr_t a,uint64_t g,int hot_enabled){
    if(g&1)return NULL;
    if(c->next) {
        Region* recent=&c->recent[(c->next-1)&7u];
        if(tlou_hot_region_contains(recent->generation,g,recent->start,recent->end,a)) {
            ++c->recent_hits;return recent;
        }
    }
    size_t slot=tlou_hot_region_slot(a);
    if(hot_enabled) {
        Region* hot=&c->hot[slot];
        if(tlou_hot_region_contains(hot->generation,g,hot->start,hot->end,a)){
            ++c->hot_hits;return hot;
        }
    }
    for(unsigned k=0;k<8;k++) {
        Region* v=&c->recent[k];
        if(tlou_hot_region_contains(v->generation,g,v->start,v->end,a)) {
            if (hot_enabled) {c->hot[slot]=*v;return &c->hot[slot];}
            return v;
        }
    }
    Region* r=expected(a);
    ++c->fallback;
    if(r) {
        Region entry=*r;entry.generation=g;
        c->recent[c->next++ &7u]=entry;
        if(hot_enabled) {c->hot[slot]=entry;return &c->hot[slot];}
        return &c->recent[(c->next-1)&7u];
    }
    return NULL;
}
static int same(const Region* a,const Region* b) {
    return a==b || (a && b && a->start==b->start && a->end==b->end &&
                    a->kind==b->kind && a->phys==b->phys);
}
static double bench(Lookup* c, const uintptr_t* queries, size_t n,uint64_t g,int hot) {
    struct timespec st,en;
    clock_gettime(CLOCK_MONOTONIC,&st);
    uint64_t sum=0;
    for(size_t i=0;i<n;i++) {
        const Region* r=lookup(c,queries[i],g,hot);
        if(r)sum+=r->phys;
    }
    clock_gettime(CLOCK_MONOTONIC,&en);
    checksum=checksum+sum;
    return (en.tv_sec-st.tv_sec)*1e9+(en.tv_nsec-st.tv_nsec);
}
int main(void) {
    const uintptr_t base=(uintptr_t)0x100000000ULL;
    for(unsigned i=0;i<VMAS;i++) {
        table[i]=(Region){2,base+(uintptr_t)i*0x2000,
                          base+(uintptr_t)i*0x2000+0x1000,
                          (i%17==0)?1:0,(uint64_t)i*4096};
    }
    Lookup old={0},newc={0};
    for(unsigned i=0;i<900000;i++) {
        const uintptr_t a=base+(uintptr_t)(rnd()%VMAS)*0x2000+
                          (rnd()%0x2000); /* half mapped, half gap */
        const uint64_t g=2+(i/300000)*2;
        if(i==300000 || i==600000) {
            const unsigned at=25+(rnd()%500);
            table[at].kind ^=1;
            table[at].phys+=12345;
        }
        const Region *a0=lookup(&old,a,g,0),*b0=lookup(&newc,a,g,1);
        assert(same(a0,b0));
        if(a0)assert(same(a0,expected(a)));
    }
    /* Same physical 4-KiB page can hold two mappings with different byte
     * boundaries. Exact start/end validation must prevent wrong reuse. */
    const uintptr_t x=base+0x800;
    table[0].end=x;
    Lookup sub={0};
    assert(lookup(&sub,base+1,400,1));
    assert(lookup(&sub,x+1,400,1)==NULL);
    assert(lookup(&sub,base+1,402,1)); /* generation invalidated */
    assert(lookup(&sub,base+1,403,1)==NULL); /* write in progress */
    const size_t N=2000000;
    static uintptr_t queries[2000000];
    uintptr_t hot_pages[64];unsigned found=0;
    for(unsigned i=1;i<VMAS && found<64;i++) {
        uintptr_t addr=base+(uintptr_t)i*0x2000+512;
        int distinct=1;
        for(unsigned j=0;j<found;j++)
            if(tlou_hot_region_slot(hot_pages[j])==tlou_hot_region_slot(addr)) distinct=0;
        if(distinct)hot_pages[found++]=addr;
    }
    assert(found==64);
    for(size_t i=0;i<N;i++)queries[i]=hot_pages[(i*73+(i/31))%64];
    double times_old[5],times_hot[5];
    for(int run=0;run<5;run++) {
        Lookup a={0},b={0};
        if(run&1) {
            times_hot[run]=bench(&b,queries,N,500,1)/N;
            times_old[run]=bench(&a,queries,N,500,0)/N;
        } else {
            times_old[run]=bench(&a,queries,N,500,0)/N;
            times_hot[run]=bench(&b,queries,N,500,1)/N;
        }
        assert(a.fallback > N/2);
        assert(b.hot_hits > N/2);
    }
    for(int i=0;i<5;i++)for(int j=i+1;j<5;j++){
        if(times_hot[j]<times_hot[i]){double t=times_hot[i];times_hot[i]=times_hot[j];times_hot[j]=t;}
        if(times_old[j]<times_old[i]){double t=times_old[i];times_old[i]=times_old[j];times_old[j]=t;}
    }
    printf("TLOU native VMA hot-cache: 900k exact randomized reads with remaps "
           "and same-page split PASS; 2M 64-resource revisit old=%.2fns hot=%.2fns "
           "ratio=%.3f (host, not game FPS)\n",
           times_old[2],times_hot[2],times_hot[2]/times_old[2]);
    assert(times_hot[2]<times_old[2]); /* no integration without CPU savings */
}
