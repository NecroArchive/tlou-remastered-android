// SPDX-License-Identifier: GPL-2.0-or-later
// Compares the exact seven-byte "OrbShdr" predicate and earliest match.
#include "../gpu/shadps4/video_core/amdgpu/tlou_shader_signature_scan.h"
#include <array>
#include <bit>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string_view>
using u32 = std::uint32_t;
static constexpr std::array<unsigned char, 7> Signature =
    {0x4f,0x72,0x62,0x53,0x68,0x64,0x72};
static constexpr u32 Prefix = 0x5362724fu;
static_assert(std::bit_cast<u32>(std::array<unsigned char,4>{0x4f,0x72,0x62,0x53})==Prefix);
struct Info {std::array<unsigned char,7> signature;unsigned char version;std::array<u32,6> rest;
    bool Valid()const{return signature==Signature;}};
static const u32* scalar(const u32* p,const u32* end) {
    for(;p<end;++p) {
        const auto* info=reinterpret_cast<const Info*>(p);
        if(info->Valid())return p;
    }
    return nullptr;
}
static const u32* accel(const u32* p,const u32* end) {
    return AmdGpu::Detail::FindShaderSignature32(
        p,end,Prefix,[](const u32* q) {
            return reinterpret_cast<const Info*>(q)->Valid();
        });
}
static void correctness(){
    std::mt19937_64 rng(0x53a5);
    alignas(64) std::array<u32,0x4000+16> words{};
    for(int trial=0;trial<5000;++trial){
        for(auto& w:words)w=static_cast<u32>(rng());
        const size_t offset=(trial%8==0)?0x3fff:
            (trial%8==1)?0x3ffc:
            (trial%8==2)?1:
            (trial%8==3)?0:
            static_cast<size_t>(rng()%0x4000);
        std::memcpy(words.data()+offset,Signature.data(),7);
        // A decoy with identical first DWORD must not hide the real hit.
        if(offset>5 && trial%3==0){
            const size_t decoy=offset-5;
            std::memcpy(words.data()+decoy,Signature.data(),4);
            reinterpret_cast<unsigned char*>(words.data()+decoy)[4]=0x00;
        }
        const u32* a=scalar(words.data(),words.data()+0x4000);
        const u32* b=accel(words.data(),words.data()+0x4000);
        if(a!=b){std::fprintf(stderr,"Mismatch trial=%d offset=%zu\n",trial,offset);std::abort();}
    }
    std::puts("shader signature SIMD: 5000 randomized positions, decoys and tails PASS");
}
static volatile std::uintptr_t sink=0;
static double benchmark(std::array<u32,0x4000+16>& words,bool use_simd){
    auto begin=std::chrono::steady_clock::now();
    std::uintptr_t out=0;
    for(int n=0;n<6000;++n){
        asm volatile("" ::: "memory");
        out+=reinterpret_cast<std::uintptr_t>(
            use_simd ? accel(words.data(),words.data()+0x4000)
                     : scalar(words.data(),words.data()+0x4000));
    }
    sink=out;
    return std::chrono::duration<double,std::nano>(
        std::chrono::steady_clock::now()-begin).count()/6000.;
}
int main(int argc,char**argv){
    correctness();
    if(argc>1 && std::string_view(argv[1])=="--correctness-only")return 0;
    std::mt19937_64 rng(0x7867);
    alignas(64) std::array<u32,0x4000+16> words{};
    for(const int offset:{12,80,400,1800,8000,15000}){
        for(auto& w:words)w=static_cast<u32>(rng());
        std::memcpy(words.data()+offset,Signature.data(),7);
        assert(scalar(words.data(),words.data()+0x4000)==
               accel(words.data(),words.data()+0x4000));
        double old=0,neo=0;
        for(int repeat=0;repeat<5;++repeat){
            if(repeat&1){neo+=benchmark(words,true);old+=benchmark(words,false);}
            else{old+=benchmark(words,false);neo+=benchmark(words,true);}
        }
        std::printf("offset_dw=%d scalar=%.2f ns SIMD=%.2f ns speedup=%.2fx saved=%.2f ns\n",
                    offset,old/5,neo/5,old/neo,(old-neo)/5);
    }
}
