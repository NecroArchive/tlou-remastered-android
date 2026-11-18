// SPDX-License-Identifier: GPL-2.0-or-later
// Compare the production Shader::SharpFetch code with the ORIGINAL loop,
// including randomized immediate words, mixed offsets and UNKNOWN_LOCATION.
// This measures one frequent Stage-A descriptor read, not the game's FPS.
#include "shader_recompiler/resource.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string_view>

using u32=std::uint32_t;
using u64=std::uint64_t;
using Fetch=Shader::SharpFetch<AmdGpu::Buffer>;
static_assert(sizeof(AmdGpu::Buffer)==4*sizeof(u32));

template<class T,u32 N=sizeof(T)/sizeof(u32)>
bool LegacyFetch(const Shader::SharpFetch<T>& f,const u32* flat,T* out){
    u8 mask=f.load_mask;
    for(u32 i=0;i<N;++i)if(f.offsets[i]==Shader::UNKNOWN_LOCATION)return false;
    std::array<u32,N> words;
    for(u32 i=0;i<N;++i) {
        words[i]=(mask&1)?flat[f.offsets[i]]:f.immediates[i];
        mask>>=1;
    }
    std::memcpy(out,words.data(),sizeof(words));
    return true;
}
static void Correctness(){
    std::mt19937_64 rng(0x24d0a981);
    std::array<u32,512> flat{};
    for(auto& x:flat)x=u32(rng());
    for(unsigned sample=0;sample<350000;++sample){
        Fetch f{};
        const unsigned mode=sample%7;
        for(unsigned j=0;j<4;++j) {
            f.immediates[j]=u32(rng());
            f.offsets[j]=u16(rng()%flat.size());
        }
        if(mode==0 || mode==1 || mode==2 || mode==3) {
            const u16 off=u16(rng()%(flat.size()-4));
            for(unsigned j=0;j<4;++j)f.offsets[j]=off+j;
            f.load_mask=mode==3?u8(0x7):u8(0xF);
        } else {
            f.load_mask=u8(rng()&0xF);
            if(mode==6)f.offsets[rng()%4]=Shader::UNKNOWN_LOCATION;
        }
        AmdGpu::Buffer old_data{},new_data{};
        std::memset(&old_data,0xA5,sizeof(old_data));
        std::memset(&new_data,0xA5,sizeof(new_data));
        const bool a=LegacyFetch(f,flat.data(),&old_data);
        const bool b=f.Fetch(flat.data(),&new_data);
        assert(a==b);
        assert(!std::memcmp(&old_data,&new_data,sizeof(old_data)));
    }
    std::puts("SRT sharp fetch parity: 350000 varied descriptors, full/partial mask, unknown offsets PASS");
}
static volatile u64 sink=0;
static __attribute__((noinline)) double Benchmark(bool next,bool consecutive){
    std::array<u32,512> flat{};
    for(unsigned j=0;j<flat.size();++j)flat[j]=j*0x1904a97u+17u;
    Fetch f{};
    f.load_mask=0xF;
    for(unsigned j=0;j<4;++j){
        f.immediates[j]=unsigned(j)*17;
        f.offsets[j]=u16(100+(consecutive?j:j*3));
    }
    AmdGpu::Buffer out{};
    u64 checksum=0;
    constexpr unsigned Repeats=1400000;
    const auto start=std::chrono::steady_clock::now();
    for(unsigned i=0;i<Repeats;++i){
        // The guest overwrites a descriptor word between individual dispatches;
        // no hoisting through the synthetic dispatch boundary is allowed.
        flat[100+(consecutive?0:0)]=i*73+19;
        std::atomic_signal_fence(std::memory_order_seq_cst);
        const bool valid=next?f.Fetch(flat.data(),&out):LegacyFetch(f,flat.data(),&out);
        checksum+=valid?reinterpret_cast<const u32*>(&out)[i&3]:0;
    }
    sink=sink+checksum;
    return std::chrono::duration<double,std::nano>(
        std::chrono::steady_clock::now()-start).count()/Repeats;
}
static void Cost(bool gate) {
    for(bool contiguous:{true,false}) {
        std::array<double,9> old{},next{};
        for(unsigned i=0;i<9;++i) {
            if(i&1){
                next[i]=Benchmark(true,contiguous);
                old[i]=Benchmark(false,contiguous);
            }else{
                old[i]=Benchmark(false,contiguous);
                next[i]=Benchmark(true,contiguous);
            }
        }
        std::sort(old.begin(),old.end());
        std::sort(next.begin(),next.end());
        const double ratio=next[4]/old[4];
        std::printf("actual SRT GetSharp 4-word %s: legacy=%.2fns direct=%.2fns ratio=%.3f\\n",
            contiguous?"contiguous":"noncontiguous",old[4],next[4],ratio);
        if(gate && (contiguous?ratio>0.85:ratio>1.20)) {
            std::fprintf(stderr,"SRT sharp fetch cost gate failed: %s ratio %.3f\\n",
                         contiguous?"contiguous":"noncontiguous",ratio);
            std::abort();
        }
    }
}
int main(int argc,char**argv){
    Correctness();
    if(argc==2 && std::string_view(argv[1])=="--correctness-only")return 0;
    if(argc==2 && std::string_view(argv[1])=="--cost-gate"){Cost(true);return 0;}
    if(argc==1){Cost(false);return 0;}
    return 2;
}
