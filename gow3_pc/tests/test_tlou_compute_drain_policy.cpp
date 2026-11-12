// SPDX-License-Identifier: GPL-2.0-or-later
// Exact production policy, tested on x86-64 and ARM64; NOT game FPS.
#include "video_core/renderer_vulkan/tlou_compute_drain_policy.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <initializer_list>
#include <random>
#include <vector>
using AmdGpu::PM4ItOpcode;
using Vulkan::Detail::ClassifyComputePreDrain;
using Vulkan::Detail::ComputePreDrain;
using Vulkan::Detail::CanSkipComputePreDrain;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
static void Policy() {
    static_assert(ClassifyComputePreDrain(PM4ItOpcode::IndirectBuffer)==ComputePreDrain::NestedBuffer);
    static_assert(ClassifyComputePreDrain(PM4ItOpcode::AcquireMem)==ComputePreDrain::NoEffect);
    static_assert(ClassifyComputePreDrain(PM4ItOpcode::EventWrite)==ComputePreDrain::NoEffect);
    static_assert(ClassifyComputePreDrain(PM4ItOpcode::WriteData)==ComputePreDrain::InnerDrain);
    for(auto opcode:{PM4ItOpcode::WaitRegMem,PM4ItOpcode::DmaData,
                     PM4ItOpcode::Rewind,PM4ItOpcode::MemSemaphore,
                     PM4ItOpcode::ReleaseMem,PM4ItOpcode::DispatchDirect}) {
        assert(ClassifyComputePreDrain(opcode)==ComputePreDrain::Required);
        assert(!CanSkipComputePreDrain(ClassifyComputePreDrain(opcode),false,true));
    }
    assert(!CanSkipComputePreDrain(ComputePreDrain::NestedBuffer,true,true));
    assert(!CanSkipComputePreDrain(ComputePreDrain::NestedBuffer,false,false));
    assert(CanSkipComputePreDrain(ComputePreDrain::NestedBuffer,false,true));
}
struct Write {u32 address,value;};
struct Memory {
    std::array<u32,1024> words{};
    std::vector<Write> pending;
    void Queue(u32 address,u32 value){pending.push_back({address,value});}
    bool Overlaps(u32 address,u32 size) const {
        for(const auto& w:pending)if(w.address>=address&&w.address-address<size)return true;
        return false;
    }
    void Drain(){for(const auto& w:pending)words[w.address]=w.value;pending.clear();}
};
static void ByteParity() {
    std::mt19937 gen{0xACB2026};
    Memory old{},optimized{};
    u64 skipped=0,guarded=0;
    // Before reading the nested IB, the optimized policy retains the drain
    // iff any pending Stage-B guest write overlaps that exact IB region.
    for(u32 i=0;i<200000;++i) {
        if((gen()%7)<3){
            u32 addr=gen()%1024,value=gen();
            old.Queue(addr,value);optimized.Queue(addr,value);
        }
        u32 start=gen()%1016,n=1+gen()%8;
        bool hazard=optimized.Overlaps(start,n);
        old.Drain();
        if(hazard){optimized.Drain();++guarded;}else ++skipped;
        for(u32 p=start;p<start+n;++p)assert(old.words[p]==optimized.words[p]);
        if(gen()%31==0){old.Drain();optimized.Drain();}
    }
    old.Drain();optimized.Drain();
    assert(old.words==optimized.words);
    assert(skipped>guarded);
    std::printf("nested-IB guest command parity: 200000 reads, %llu safe, %llu guarded PASS\n",
         (unsigned long long)skipped,(unsigned long long)guarded);
}
static void Cost() {
    std::mt19937 gen{0x20261111};
    // SYNTHETIC guest packet mixture, not a captured TLOU workload.
    constexpr std::array<PM4ItOpcode,9> ops{
        PM4ItOpcode::IndirectBuffer,PM4ItOpcode::IndirectBuffer,
        PM4ItOpcode::IndirectBuffer,PM4ItOpcode::WaitRegMem,
        PM4ItOpcode::DmaData,PM4ItOpcode::WriteData,
        PM4ItOpcode::EventWrite,PM4ItOpcode::AcquireMem,PM4ItOpcode::Rewind};
    u64 baseline=0,remaining=0,nested=0,hazards=0;
    for(u32 i=0;i<900000;++i){
        auto op=ops[gen()%ops.size()];
        bool hazard=(gen()%11)==0;
        auto kind=ClassifyComputePreDrain(op);
        bool skip=CanSkipComputePreDrain(kind,hazard,true);
        ++baseline;remaining+=!skip;
        if(kind==ComputePreDrain::NestedBuffer){
            ++nested;hazards+=hazard;assert(!hazard||!skip);
        }
        if(op==PM4ItOpcode::WaitRegMem||op==PM4ItOpcode::DmaData)assert(!skip);
    }
    assert(remaining<baseline/2);
    std::printf("synthetic compute predrains: %llu -> %llu (%.1f%% removed); "
                "nested=%llu guarded=%llu (NOT FPS)\n",
         (unsigned long long)baseline,(unsigned long long)remaining,
         100.0*(baseline-remaining)/baseline,
         (unsigned long long)nested,(unsigned long long)hazards);
}
int main(){Policy();ByteParity();Cost();}
