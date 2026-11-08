// SPDX-License-Identifier: GPL-2.0-or-later
// Native ARM64/x86 microbenchmark; this is NOT an APK or an in-game FPS claim.
#include "../gpu/shadps4/video_core/renderer_vulkan/tlou_pending_bloom_words.h"
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <string_view>
using Filter=std::array<std::uint64_t,64>;
struct Range { std::uint64_t begin,end; };
static volatile std::uint64_t sink=0;
static void legacy(Filter& f,std::uint64_t begin,std::uint64_t end) {
    const auto first=begin>>16,last=(end-1)>>16;
    if(last-first>=4096){f.fill(~std::uint64_t{0});return;}
    for(std::uint64_t granule=first;granule<=last;++granule){
        const auto bit=std::uint32_t(granule%4096);
        f[bit/64]|=std::uint64_t{1}<<(bit%64);
    }
}
static void fast(Filter& f,std::uint64_t begin,std::uint64_t end) {
    Vulkan::Detail::MarkPendingBloomWords<16>(f,begin,end);
}
using Func=void(*)(Filter&,std::uint64_t,std::uint64_t);
static double cost(const std::vector<Range>& ranges, Func f) {
    const auto t0=std::chrono::steady_clock::now();
    Filter filter{};
    for(int repeat=0;repeat<5;++repeat)for(const auto& r:ranges)f(filter,r.begin,r.end);
    std::uint64_t checksum=0;
    for(const auto value:filter)checksum^=value;
    sink=checksum;
    return std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-t0).count() /
           (5*ranges.size());
}
int main(int argc,char**argv) {
    const bool only_correct=argc>1 && std::string_view(argv[1])=="--correctness-only";
    std::mt19937_64 rng(0x78453);
    // Match the ORIGINAL bit masks, including 4096-bit saturation and modulo wrap.
    for(int t=0;t<120000;++t) {
        Filter a{},b{};
        const std::uint64_t g=rng()%(1ull<<27);
        const std::uint64_t begin=(g<<16)+(rng()&65535);
        const std::uint64_t n=(t%6==0)?4097u:1u+(rng()%4400);
        const std::uint64_t end=begin+n*65536;
        legacy(a,begin,end); fast(b,begin,end);
        if(a!=b)throw std::runtime_error("Different bloom bits after update");
        // Repeated overlapping writes must preserve the same union of bits.
        legacy(a,begin+16,end); fast(b,begin+16,end);
        if(a!=b)throw std::runtime_error("Different accumulated bloom bits");
    }
    std::puts("pending-write bloom: byte-exact parity PASS");
    if(only_correct)return 0;
    for(int dist=0;dist<3;++dist) {
        std::vector<Range> items;
        items.reserve(250000);
        for(int i=0;i<250000;++i) {
            const auto start=((rng()%(1ull<<27))<<16)+(rng()&65535);
            std::uint64_t n=1+rng()%2;
            if(dist==1 && (i%5)==0)n=8+rng()%128;
            if(dist==2)n=64+rng()%500;
            items.push_back({start,start+n*65536});
        }
        double old_ns=0,new_ns=0;
        // Alternate order to minimize temperature and CPU frequency bias.
        for(int rep=0;rep<3;++rep) {
            if(rep&1){new_ns+=cost(items,fast);old_ns+=cost(items,legacy);}
            else {old_ns+=cost(items,legacy);new_ns+=cost(items,fast);}
        }
        old_ns/=3;new_ns/=3;
        std::printf("scenario=%s count=%zu legacy=%.2f ns/write words=%.2f ns/write "
                    "speedup=%.2fx delta=%.2f ns/write\n",
                    dist==0?"short":dist==1?"mixed":"large",items.size(),
                    old_ns,new_ns,old_ns/new_ns,old_ns-new_ns);
    }
}
