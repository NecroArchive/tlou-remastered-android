// SPDX-License-Identifier: GPL-2.0-or-later
// Calls actual runtime_memory_clamp/copy_backing implementation, not replicas.
// Synthetic VMA distribution, not a captured TLOU sequence.
#define _GNU_SOURCE
#include "../src/runtime_memory.c"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
int32_t *runtime_errno(void) { static __thread int32_t err; return &err; }
static volatile uint64_t escape_sum;
static const unsigned VmCount=1024;
static const unsigned Steps=110000;
static uint64_t now_ns(void) {
  struct timespec t; clock_gettime(CLOCK_MONOTONIC_RAW,&t);
  return (uint64_t)t.tv_sec*1000000000ull+(uint64_t)t.tv_nsec;
}
static void init_vm(void) {
  vmas=calloc(VmCount,sizeof(*vmas)); assert(vmas);
  vma_count=vma_capacity=VmCount;
  backing_base=malloc(1u<<20);assert(backing_base);
  for(size_t i=0;i<1u<<20;++i)backing_base[i]=(unsigned char)(i*13u);
  for(unsigned i=0;i<VmCount;++i) {
    vmas[i].start=USER_MIN+(uintptr_t)i*0x100000;
    vmas[i].end=vmas[i].start+0x100000;
    vmas[i].kind=KIND_DIRECT; vmas[i].phys=0;vmas[i].prot=3;
  }
  table_generation=2;
}
static void reset_cache(int direct) {
  direct_region_cache_mode=direct;
  region_cache_next=0;memset(region_cache,0,sizeof region_cache);
  memset(direct_region_cache,0,sizeof direct_region_cache);
}
static void check_equivalent(void) {
  unsigned char a[256],b[256];
  for (unsigned mode=0;mode<=1;++mode) {
    reset_cache(mode);
    for(unsigned i=0;i<VmCount;++i) {
      uintptr_t addr=vmas[i].start+256;
      assert(runtime_memory_clamp(addr,4096)==4096);
      runtime_memory_copy_backing(addr,a,sizeof a);
      for(unsigned j=0;j<sizeof a;++j) assert(a[j]==backing_base[256+j]);
      const uint64_t available=(VmCount-i)*0x100000ull-256;
      const uint64_t request=0x1FFFFFull;
      assert(runtime_memory_clamp(addr,request)==(request<available?request:available));
    }
    uintptr_t addr=vmas[17].start+256;
    assert(runtime_memory_clamp(addr,256)==256);
    vmas[17].kind=KIND_RESERVED;
    __atomic_add_fetch(&table_generation,2,__ATOMIC_RELEASE);
    assert(runtime_memory_clamp(addr,256)==0);
    runtime_memory_copy_backing(addr,a,sizeof a);
    memset(b,0,sizeof b);assert(memcmp(a,b,sizeof a)==0);
    vmas[17].kind=KIND_DIRECT;
    __atomic_add_fetch(&table_generation,2,__ATOMIC_RELEASE);
    assert(runtime_memory_clamp(addr,256)==256);
    runtime_memory_copy_backing(addr,a,sizeof a);
    assert(memcmp(a,backing_base+256,sizeof a)==0);
  }
  reset_cache(0); // conservatively leave production default disabled
}
enum {Clamped,Copy256,Copy4096};
static double trial(int mode,unsigned working_set,unsigned operation) {
  reset_cache(mode);
  // deterministic 8/64/256/1024 region walk, same addresses in both modes.
  const uintptr_t base=USER_MIN;
  unsigned char dst[4096];
  uint64_t sum=0;
  const uint64_t start=now_ns();
  for(unsigned i=0;i<Steps;++i) {
    const unsigned idx=(i*1193u+23u)%working_set;
    uintptr_t addr=base+(uintptr_t)idx*0x100000u+(i&1023u);
    if(operation==Clamped) sum+=runtime_memory_clamp(addr,4096);
    else if(operation==Copy256) {
      runtime_memory_copy_backing(addr,dst,256);
      sum+=dst[0]+dst[255];
    } else {
      runtime_memory_copy_backing(addr,dst,4096);
      sum+=dst[0]+dst[4095];
    }
    __asm__ __volatile__("" ::: "memory");
  }
  escape_sum=sum;
  return (double)(now_ns()-start)/Steps;
}
static double median(int mode,unsigned set,unsigned op) {
  double x[7];
  for(unsigned i=0;i<7;++i)x[i]=trial(mode,set,op);
  for(unsigned i=0;i<7;++i)for(unsigned j=i+1;j<7;++j)
    if(x[j]<x[i]){double t=x[i];x[i]=x[j];x[j]=t;}
  return x[3];
}
int main(int argc,char**argv) {
  init_vm();check_equivalent();
  if(argc>1&&!strcmp(argv[1],"--correctness-only")) {
    puts("VMA cached region generation, reserved mappings, copy and clamps: PASS");
    return 0;
  }
  for(unsigned set=8;set<=1024;set*=4) {
    for(unsigned op=Clamped;op<=Copy4096;++op) {
      trial(0,set,op);trial(1,set,op);
      const double before=median(0,set,op),after=median(1,set,op);
      const double delta=before-after;
      printf("REAL_VMA set=%u operation=%s old=%.2f direct=%.2f saved=%.2f ns/operation; at 7013 copies/frame = %.4f ms/frame (if every operation same)\\n",
        set,op==Clamped?"clamp":op==Copy256?"copy256":"copy4096",
        before,after,delta,7013.0*delta/1e6);
    }
  }
  puts("7013 copies/frame is from the older profiled TLOU run (792504/5sec at 22.6fps), not a new-run hit histogram.");
  free(backing_base);free(vmas);
}
