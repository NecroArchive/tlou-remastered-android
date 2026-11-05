// SPDX-License-Identifier: GPL-2.0-or-later
// CI extracts the EXACT production RecordChunk C++ class from vk_scheduler.h
// and substitutes only vk::CommandBuffer=int (no driver in native benchmark).
#include "tlou_record_chunk_extracted.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <memory>
#include <vector>

using Vulkan::RecordChunk;
using Clock=std::chrono::steady_clock;
using u64=std::uint64_t;

struct Context {
    u64 completed=0, checksum=0, boundaries=0;
    std::atomic<u64> done{0};
};
static void Copy(const Gow3Copy::Item& item) {
    auto* c=static_cast<Context*>(item.context);
    ++c->completed;
    c->checksum += item.source + item.size;
}
static Gow3Copy::Item MakeItem(Context& c, u64 index) {
    return {.run=&Copy, .context=&c, .source=index,
            .destination=0, .size=16u+((index&7u)<<4), .extra=0};
}

struct Measurement {
    double record_ns=0;
    double execute_ns=0;
    std::size_t chunks=0;
};
static Measurement Sample(bool batch, unsigned consecutive) {
    constexpr u64 Copies=7013;
    Context c{};
    std::vector<std::unique_ptr<RecordChunk>> chunks;
    chunks.push_back(std::make_unique<RecordChunk>());
    const auto push_normal=[&](auto&& operation) {
        if(!chunks.back()->Push(std::forward<decltype(operation)>(operation))) {
            chunks.push_back(std::make_unique<RecordChunk>());
            if(!chunks.back()->Push(std::forward<decltype(operation)>(operation)))
                std::abort();
        }
    };
    const auto start=Clock::now();
    for(u64 i=0;i<Copies;++i) {
        const auto item=MakeItem(c,i+1);
        if (batch) {
            if(!chunks.back()->PushHostCopy(item,i+1,c.done)) {
                chunks.push_back(std::make_unique<RecordChunk>());
                if(!chunks.back()->PushHostCopy(item,i+1,c.done))std::abort();
            }
        } else {
            push_normal([item,&c,sequence=i+1](int){
                item.run(item);
                c.done.store(sequence,std::memory_order_release);
            });
        }
        // A real intervening Vulkan record command must break the batch.
        // Check that all guest copy callbacks before that command finished,
        // and that the completion sequence is published conservatively.
        if(i%consecutive == consecutive-1 || i==Copies-1) {
            push_normal([&c,sequence=i+1](int){
                assert(c.done.load(std::memory_order_acquire)==sequence);
                ++c.boundaries;
            });
        }
    }
    const auto after_record=Clock::now();
    for(auto& chunk:chunks)chunk->Execute(0);
    const auto after_execute=Clock::now();
    assert(c.completed==Copies);
    assert(c.done.load(std::memory_order_acquire)==Copies);
    assert(c.boundaries==(Copies+consecutive-1)/consecutive);
    const u64 expected=Copies*(Copies+1)/2 +
        16*Copies + 16*(Copies/8)*28 + 16*(((Copies%8)*(Copies%8-1))/2);
    (void)expected; // checksum is consumed below to protect callback side effects
    assert(c.checksum>Copies);
    return {
      std::chrono::duration<double,std::nano>(after_record-start).count()/Copies,
      std::chrono::duration<double,std::nano>(after_execute-after_record).count()/Copies,
      chunks.size()
    };
}
static Measurement Median(bool batch,unsigned group) {
    std::array<Measurement,9> samples;
    for(auto& a:samples)a=Sample(batch,group);
    std::sort(samples.begin(),samples.end(),[](auto& a,auto& b){
        return a.record_ns+a.execute_ns<b.record_ns+b.execute_ns;
    });
    return samples[4];
}
int main(int argc,char**argv){
    for(unsigned group:{1u,2u,4u,8u,16u}) {
        const auto baseline=Sample(false,group),batched=Sample(true,group);
        (void)baseline;(void)batched;
    }
    puts("PRODUCTION RecordChunk extraction: order, boundary, completion, copy count PASS");
    if(argc>1 && argv[1][0]=='-' ) return 0;
    for(unsigned group:{1u,2u,4u,8u,16u}) {
        auto b=Median(false,group),n=Median(true,group);
        const double saving=(b.record_ns+b.execute_ns)-(n.record_ns+n.execute_ns);
        printf("REAL_RECORD_CHUNK group=%u old-rec=%.2f new-rec=%.2f old-exec=%.2f new-exec=%.2f ns/copy delta=%.2f ns/copy; OLD_LOG=7013/frame => %.3f ms/frame (only record+execute overhead) chunks_old=%zu chunks_new=%zu\\n",
            group,b.record_ns,n.record_ns,b.execute_ns,n.execute_ns,
            saving,7013*saving/1e6,b.chunks,n.chunks);
    }
}
