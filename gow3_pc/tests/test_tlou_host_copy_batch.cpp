// SPDX-License-Identifier: GPL-2.0-or-later
// Production HostCopyBatch helper with the exact pending->complete release.
#include "../gpu/shadps4/video_core/renderer_vulkan/tlou_host_copy_batch.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using Clock = std::chrono::steady_clock;
using Batch = Vulkan::Detail::HostCopyBatch<16>;
using u64 = std::uint64_t;

struct Context { u64* count; u64* sum; };
static void CountCopy(const Gow3Copy::Item& it) {
    auto* ctx = static_cast<Context*>(it.context);
    ++*ctx->count;
    *ctx->sum += it.source ^ it.size;
}

static Gow3Copy::Item ItemFor(Context& ctx, u64 i) {
    return {.run = &CountCopy, .context=&ctx, .source=i,
            .destination=0, .size=32+(i%16)*16, .extra=0};
}

static void Verify() {
    std::atomic<u64> complete{0};
    u64 count=0,sum=0;
    Context ctx{&count,&sum};
    {
        Batch b{complete,ItemFor(ctx,1),1};
        assert(b.Size()==1);
        for(u64 i=2;i<=16;++i)assert(b.Append(ItemFor(ctx,i),i));
        assert(!b.Append(ItemFor(ctx,17),17));
        assert(!b.Append(ItemFor(ctx,19),16));
        assert(complete.load()==0); // guest-visible done must not advance until Execute
        b.Execute();
        assert(count==16 && complete.load(std::memory_order_acquire)==16);
    }
    // A new batch after an intervening Vulkan command begins independently;
    // old completed order is visible first.
    Batch other{complete,ItemFor(ctx,17),17};
    assert(complete.load(std::memory_order_acquire)==16);
    other.Execute();
    assert(count==17 && complete.load(std::memory_order_acquire)==17);
    std::printf("HostCopyBatch: sequence order, full batch, copied data, final release store PASS\\n");
}

static double Benchmark(bool batched) {
    constexpr u64 Copies=7013;
    constexpr u64 Repetitions=250;
    u64 total=0,sum=0;
    Context ctx{&total,&sum};
    std::atomic<u64> done{0};
    const auto start=Clock::now();
    for(u64 frame=0;frame<Repetitions;++frame) {
        if(batched) {
            for(u64 begin=0;begin<Copies;begin+=16) {
                Batch batch{done,ItemFor(ctx,frame*Copies+begin+1),frame*Copies+begin+1};
                for(u64 x=begin+1;x<std::min(Copies,begin+16);++x) {
                    assert(batch.Append(ItemFor(ctx,frame*Copies+x+1),frame*Copies+x+1));
                }
                batch.Execute();
            }
        } else {
            for(u64 i=0;i<Copies;++i) {
                const auto it=ItemFor(ctx,frame*Copies+i+1);
                it.run(it);
                done.store(frame*Copies+i+1,std::memory_order_release);
            }
        }
    }
    assert(total==Copies*Repetitions);
    assert(done.load(std::memory_order_acquire)==Copies*Repetitions);
    volatile u64 sink=sum;(void)sink;
    return std::chrono::duration<double,std::nano>(Clock::now()-start).count()/(Copies*Repetitions);
}

int main(int argc,char** argv) {
    Verify();
    if(argc>1 && std::strcmp(argv[1],"--correctness-only")==0)return 0;
    for(int i=0;i<2;++i){Benchmark(false);Benchmark(true);}
    std::array<double,7> base{},batched{};
    for(std::size_t i=0;i<base.size();++i){
        if(i&1){batched[i]=Benchmark(true);base[i]=Benchmark(false);}
        else{base[i]=Benchmark(false);batched[i]=Benchmark(true);}
    }
    std::sort(base.begin(),base.end());std::sort(batched.begin(),batched.end());
    const double saved=base[3]-batched[3];
    std::printf("Host-copy callback and completion microbenchmark: old=%.2f ns/copy, batched=%.2f ns/copy, delta=%.2f ns/copy\\n",
                base[3],batched[3],saved);
    std::printf("7013 old-profile copies/frame: %.4f ms/frame ONLY on tested callback+release portion; Vulkan record-object costs excluded\\n",
                saved*7013/1e6);
}
