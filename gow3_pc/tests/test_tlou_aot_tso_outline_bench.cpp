// SPDX-License-Identifier: GPL-2.0-or-later
// ARM64 microbench, faithful to fixed guest op size at codegen time.
// v4: two DMB + native-sized unaligned access INLINED at each operation.
// v5: same aligned atomic, but a call to shared cold full-fence fallback.
// This is not an x86-TSO or on-device FPS proof.
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
extern "C" std::uint64_t bbr_tso_unaligned_load(std::uint64_t,std::uint32_t);
extern "C" void bbr_tso_unaligned_store(std::uint64_t,std::uint64_t,std::uint32_t);
using U64=std::uint64_t;
constexpr unsigned Iterations=500000;

// Distinct per-width native functions model the compiler's constant IR op size.
// Never put the inlined v4 fallback behind another out-of-line function.
template<unsigned Width,bool Outlined>
__attribute__((noinline)) static U64 Load(const void* p) {
  static_assert(Width==1||Width==2||Width==4||Width==8);
  if constexpr(Width==1) {
    return __atomic_load_n((const std::uint8_t*)p,__ATOMIC_ACQUIRE);
  } else {
    if((reinterpret_cast<std::uintptr_t>(p)&(Width-1))!=0) {
      if constexpr(Outlined) {
        return bbr_tso_unaligned_load((std::uintptr_t)p,Width);
      } else {
        asm volatile("dmb ish" ::: "memory");
        U64 x=0;
        __builtin_memcpy(&x,p,Width);
        asm volatile("dmb ish" ::: "memory");
        return x;
      }
    }
    if constexpr(Width==2) return __atomic_load_n((const std::uint16_t*)p,__ATOMIC_ACQUIRE);
    if constexpr(Width==4) return __atomic_load_n((const std::uint32_t*)p,__ATOMIC_ACQUIRE);
    return __atomic_load_n((const std::uint64_t*)p,__ATOMIC_ACQUIRE);
  }
}
template<unsigned Width,bool Outlined>
__attribute__((noinline)) static void Store(void* p,U64 x) {
  static_assert(Width==1||Width==2||Width==4||Width==8);
  if constexpr(Width==1) {
    __atomic_store_n((std::uint8_t*)p,(std::uint8_t)x,__ATOMIC_RELEASE);
  } else {
    if((reinterpret_cast<std::uintptr_t>(p)&(Width-1))!=0) {
      if constexpr(Outlined) {
        bbr_tso_unaligned_store((std::uintptr_t)p,x,Width);
      } else {
        asm volatile("dmb ish" ::: "memory");
        __builtin_memcpy(p,&x,Width);
        asm volatile("dmb ish" ::: "memory");
      }
      return;
    }
    if constexpr(Width==2)__atomic_store_n((std::uint16_t*)p,(std::uint16_t)x,__ATOMIC_RELEASE);
    if constexpr(Width==4)__atomic_store_n((std::uint32_t*)p,(std::uint32_t)x,__ATOMIC_RELEASE);
    if constexpr(Width==8)__atomic_store_n((std::uint64_t*)p,x,__ATOMIC_RELEASE);
  }
}
struct Result{double ns;U64 checksum;std::array<std::uint8_t,32> last;};
template<unsigned Width,bool Outlined> static Result Measure(unsigned unaligned) {
  alignas(64) std::array<std::uint8_t,32> source{},target{};
  for(unsigned j=0;j<32;++j)source[j]=std::uint8_t(j*7+3);
  U64 checksum=0;
  auto start=std::chrono::steady_clock::now();
  for(unsigned i=0;i<Iterations;++i) {
    const unsigned offset=(i%100<unaligned)?1:0;
    U64 x=Load<Width,Outlined>(source.data()+offset);
    Store<Width,Outlined>(target.data()+offset,x+(i&0xff));
    checksum+=x;
  }
  auto end=std::chrono::steady_clock::now();
  return {std::chrono::duration<double,std::nano>(end-start).count()/Iterations,checksum,target};
}
template<unsigned Width> static void Benchmark(unsigned unaligned) {
  std::array<std::vector<double>,2> times;
  for(unsigned trial=0;trial<10;++trial) {
    const auto v4=Measure<Width,false>(unaligned);
    const auto v5=Measure<Width,true>(unaligned);
    assert(v4.checksum==v5.checksum && v4.last==v5.last);
    if(trial>=2){times[0].push_back(v4.ns);times[1].push_back(v5.ns);}
  }
  for(auto& v:times) std::sort(v.begin(),v.end());
  auto old=times[0][times[0].size()/2];
  auto next=times[1][times[1].size()/2];
  std::printf("TSO v5 outline ARM64 size=%u unaligned=%u%% v4-inline=%.2fns v5-outlined=%.2fns ratio=%.3f checksum PASS\n",
              Width,unaligned,old,next,next/old);
}
int main() {
  for(unsigned ratio:{0u,10u,50u}) {
    Benchmark<1>(ratio);
    Benchmark<2>(ratio);
    Benchmark<4>(ratio);
    Benchmark<8>(ratio);
  }
}
