// SPDX-License-Identifier: GPL-2.0-or-later
// Standalone approximation of the production RecordChunk::Push layout and
// virtual recorder execution. This measures queue/closure overhead, NOT FPS.
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

using u64 = std::uint64_t;
struct PushData { std::array<std::uint32_t,32> words; };
struct Descriptor { u64 buffer, offset, range; };
static_assert(sizeof(PushData) == 128);
struct FakeVulkan {
    u64 digest=0, constant_calls=0, descriptor_calls=0;
    void PushConstants(const PushData& data) {
        digest += data.words[0] + data.words[31]; ++constant_calls;
    }
    void PushDescriptors(const Descriptor* p, std::size_t count) {
        digest += p[0].buffer + p[count-1].offset + p[count-1].range;
        ++descriptor_calls;
    }
};
class Chunk {
    static constexpr std::size_t Capacity = 128*1024;
    struct Command { virtual ~Command() = default;
                     virtual void Execute(FakeVulkan&) = 0;
                     Command* next{}; };
    template<class Fn> struct Typed final:Command {
        explicit Typed(Fn&& f):fn(std::move(f)) {}
        void Execute(FakeVulkan& v) override { fn(v); }
        Fn fn;
    };
    alignas(64) std::array<std::byte,Capacity> storage{};
    std::size_t used{};
    Command* first{};
    Command* last{};
public:
    std::size_t Size()const{return used;}
    template<class Fn> bool Push(Fn&& fn) {
        using T = Typed<std::decay_t<Fn>>;
        const auto pos=(used+alignof(T)-1)&~(alignof(T)-1);
        if(pos+sizeof(T)>Capacity)return false;
        auto* item=new(storage.data()+pos) T(std::forward<Fn>(fn));
        if(last)last->next=item;else first=item;
        last=item;used=pos+sizeof(T);
        return true;
    }
    const Descriptor* Snapshot(const std::array<Descriptor,8>& src) {
        const auto pos=(used+alignof(Descriptor)-1)&~(alignof(Descriptor)-1);
        assert(pos+sizeof(src)<Capacity);
        auto* dst=reinterpret_cast<Descriptor*>(storage.data()+pos);
        std::memcpy(dst,src.data(),sizeof(src));
        used=pos+sizeof(src);
        return dst;
    }
    void Execute(FakeVulkan& v) {
        for(auto* item=first;item;) {
            auto* next=item->next;
            item->Execute(v);item->~Command();item=next;
        }
        used=0;first=last=nullptr;
    }
};
struct Result {double ns;u64 digest, constants,descriptors;};
Result Run(unsigned mode,unsigned batches) {
    Chunk chunk;
    FakeVulkan driver;
    constexpr unsigned PerBatch = 1024;
    const unsigned calls=PerBatch*batches;
    const auto started=std::chrono::steady_clock::now();
    for(unsigned i=0;i<calls;++i) {
        PushData push{};
        push.words[0]=i; push.words[31]=i^0x1234;
        std::array<Descriptor,8> infos{};
        for(unsigned j=0;j<infos.size();++j)
            infos[j]={0x1000+j,256*((i+j)%127),512+j};
        if(chunk.Size()>120*1024)chunk.Execute(driver);
        const auto* buffers=chunk.Snapshot(infos);
        if(mode==0) {
            assert(chunk.Push([push](FakeVulkan& v){v.PushConstants(push);}));
            assert(chunk.Push([buffers](FakeVulkan& v){v.PushDescriptors(buffers,8);}));
        } else {
            assert(chunk.Push([push,buffers](FakeVulkan& v){
                v.PushConstants(push);v.PushDescriptors(buffers,8);
            }));
        }
    }
    chunk.Execute(driver);
    const auto finished=std::chrono::steady_clock::now();
    assert(driver.constant_calls==calls&&driver.descriptor_calls==calls);
    return {std::chrono::duration<double,std::nano>(finished-started).count()/calls,
            driver.digest,driver.constant_calls,driver.descriptor_calls};
}
int main() {
    std::array<std::vector<double>,2> recorded;
    for(unsigned i=0;i<20;++i) for(unsigned j=0;j<2;++j) {
        const unsigned mode=j^(i&1u);
        const auto value=Run(mode,16);
        const auto check=Run(1-mode,1);
        const auto again=Run(mode,1);
        assert(check.digest==again.digest);
        assert(check.constants==again.constants&&check.descriptors==again.descriptors);
        if(i>1)recorded[mode].push_back(value.ns);
    }
    for(auto& x:recorded)std::sort(x.begin(),x.end());
    const auto legacy=recorded[0][recorded[0].size()/2];
    const auto fused=recorded[1][recorded[1].size()/2];
    std::printf("TLOU recorder fusion synthetic: separate=%.2fns fused=%.2fns "
                "ratio=%.3f (CPU queue only, not driver/FPS)\n",legacy,fused,fused/legacy);
    assert(fused<0.98*legacy);
}
