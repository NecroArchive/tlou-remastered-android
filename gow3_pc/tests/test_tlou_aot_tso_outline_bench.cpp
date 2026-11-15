// SPDX-License-Identifier: GPL-2.0-or-later
// Native ARM64 microbench only, not evidence of full-game x86-TSO correctness.
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#if !defined(__aarch64__)
#error "ARM64 only"
#endif
extern "C" std::uint64_t bbr_tso_unaligned_load(std::uint64_t address,std::uint32_t size);
extern "C" void bbr_tso_unaligned_store(std::uint64_t address,std::uint64_t value,std::uint32_t size);
using U64=std::uint64_t;
constexpr unsigned N=500000;
__attribute__((noinline)) static U64 V4SlowLoad(const void* p,unsigned n) {
  asm volatile("dmb ish" ::: "memory");
  U64 x=0; std::memcpy(&x,p,n);
  asm volatile("dmb ish" ::: "memory");
  return x;
}
__attribute__((noinline)) static void V4SlowStore(void* p,U64 x,unsigned n) {
  asm volatile("dmb ish" ::: "memory");
  std::memcpy(p,&x,n);
  asm volatile("dmb ish" ::: "memory");
}
__attribute__((noinline)) static U64 V4Load(const void* p,unsigned n) {
  if((reinterpret_cast<std::uintptr_t>(p)&(n-1))==0) {
    switch(n) {
      case 1:return __atomic_load_n((const std::uint8_t*)p,__ATOMIC_ACQUIRE);
      case 2:return __atomic_load_n((const std::uint16_t*)p,__ATOMIC_ACQUIRE);
      case 4:return __atomic_load_n((const std::uint32_t*)p,__ATOMIC_ACQUIRE);
      case 8:return __atomic_load_n((const std::uint64_t*)p,__ATOMIC_ACQUIRE);
    }
  }
  return V4SlowLoad(p,n);
}
__attribute__((noinline)) static void V4Store(void* p,U64 x,unsigned n) {
  if((reinterpret_cast<std::uintptr_t>(p)&(n-1))==0) {
    switch(n) {
      case 1:__atomic_store_n((std::uint8_t*)p,(std::uint8_t)x,__ATOMIC_RELEASE);return;
      case 2:__atomic_store_n((std::uint16_t*)p,(std::uint16_t)x,__ATOMIC_RELEASE);return;
      case 4:__atomic_store_n((std::uint32_t*)p,(std::uint32_t)x,__ATOMIC_RELEASE);return;
      case 8:__atomic_store_n((std::uint64_t*)p,x,__ATOMIC_RELEASE);return;
    }
  }
  V4SlowStore(p,x,n);
}
__attribute__((noinline)) static U64 V5Load(const void* p,unsigned n) {
  if((reinterpret_cast<std::uintptr_t>(p)&(n-1))==0) {
    switch(n) {
      case 1:return __atomic_load_n((const std::uint8_t*)p,__ATOMIC_ACQUIRE);
      case 2:return __atomic_load_n((const std::uint16_t*)p,__ATOMIC_ACQUIRE);
      case 4:return __atomic_load_n((const std::uint32_t*)p,__ATOMIC_ACQUIRE);
      case 8:return __atomic_load_n((const std::uint64_t*)p,__ATOMIC_ACQUIRE);
    }
  }
  return bbr_tso_unaligned_load((std::uintptr_t)p,n);
}
__attribute__((noinline)) static void V5Store(void* p,U64 x,unsigned n) {
  if((reinterpret_cast<std::uintptr_t>(p)&(n-1))==0) {
    switch(n) {
      case 1:__atomic_store_n((std::uint8_t*)p,(std::uint8_t)x,__ATOMIC_RELEASE);return;
      case 2:__atomic_store_n((std::uint16_t*)p,(std::uint16_t)x,__ATOMIC_RELEASE);return;
      case 4:__atomic_store_n((std::uint32_t*)p,(std::uint32_t)x,__ATOMIC_RELEASE);return;
      case 8:__atomic_store_n((std::uint64_t*)p,x,__ATOMIC_RELEASE);return;
    }
  }
  bbr_tso_unaligned_store((std::uintptr_t)p,x,n);
}
struct Result {double ns;U64 checksum;std::array<std::uint8_t,32> last;};
static Result Measure(bool v5,unsigned n,unsigned unaligned) {
  alignas(64) std::array<std::uint8_t,32> source{},target{};
  for(unsigned j=0;j<32;++j)source[j]=std::uint8_t(j*7+3);
  U64 checksum=0;
  const auto start=std::chrono::steady_clock::now();
  for(unsigned i=0;i<N;++i) {
    unsigned offset=(i%100<unaligned)?1:0;
    const auto x=v5?V5Load(source.data()+offset,n):V4Load(source.data()+offset,n);
    if(v5)V5Store(target.data()+offset,x+(i&0xff),n);
    else V4Store(target.data()+offset,x+(i&0xff),n);
    checksum+=x;
  }
  const auto end=std::chrono::steady_clock::now();
  return {std::chrono::duration<double,std::nano>(end-start).count()/N,checksum,target};
}
int main(){
  for(unsigned n:{1u,2u,4u,8u})for(unsigned bad:{0u,10u,50u}){
    std::array<std::vector<double>,2> results;
    for(unsigned trial=0;trial<10;++trial){
      auto a=Measure(false,n,bad),b=Measure(true,n,bad);
      assert(a.checksum==b.checksum && a.last==b.last);
      if(trial>=2){results[0].push_back(a.ns);results[1].push_back(b.ns);}
    }
    for(auto& x:results)std::sort(x.begin(),x.end());
    auto old=results[0][results[0].size()/2];
    auto next=results[1][results[1].size()/2];
    std::printf("TSO v5 outline ARM64 size=%u unaligned=%u%% v4=%.2fns v5=%.2fns ratio=%.3f checksum PASS\n",
                n,bad,old,next,next/old);
  }
}
