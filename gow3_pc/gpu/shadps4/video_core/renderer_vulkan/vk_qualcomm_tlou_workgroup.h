// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <initializer_list>
#include <span>
#include <vector>
namespace Vulkan::QualcommTLOUWorkgroup {
// Exact 6020-byte shader capture from A750/Qualcomm (0x4a705408).
// 32x32 independent invocations; two readonly SSBOs, one 2D image write
// at (groupY*32+localY, groupX*32+localX). No LDS, subgroup ops or atomics.
// Execute 16 logical Y slices in a physical 32x2 group. Dispatch dimensions,
// 2D coordinates, descriptor layouts and total invocations are preserved.
inline bool Reduce(std::span<const uint32_t> src,std::vector<uint32_t>& out) {
    out.clear();
    if(src.size()!=1505 || src[0]!=0x07230203 || src[3]!=215) return false;
    uint64_t h=14695981039346656037ull;
    for(uint32_t x:src)for(unsigned shift=0;shift<32;shift+=8)
        h=(h^((x>>shift)&255u))*1099511628211ull;
    if(h!=0xb2ac7871d66934aaull)return false;
    out.assign(src.begin(),src.begin()+5);out[3]=224;
    auto add=[&](uint32_t op,std::initializer_list<uint32_t> a){
        out.push_back((uint32_t(a.size()+1)<<16)|op);
        out.insert(out.end(),a.begin(),a.end());
    };
    for(size_t i=5;i<src.size();) {
        const auto n=src[i]>>16,op=src[i]&65535u;
        if(!n||n>src.size()-i){out.clear();return false;}
        if(op==16 && n==6 && src[i+2]==17) {
            add(16,{60,17,32,2,1}); // physical workgroup
        } else if(op==54) {
            add(43,{10,215,16}); // uint loop count, before function
            out.insert(out.end(),src.begin()+i,src.begin()+i+n);
        } else if(op==248 && src[i+1]==61) {
            add(248,{61});add(249,{216});
            add(248,{216});add(245,{10,219,30,61,221,218}); // phi i
            add(176,{3,220,219,215}); // unsigned i < 16
            add(246,{62,218,0});add(250,{220,217,62});
            add(248,{217});
        } else if(op==81 && n==5 && src[i+2]==77) {
            add(81,{10,222,76,1}); // physical local Y
            add(132,{10,223,219,68}); // i*2
            add(128,{10,77,222,223}); // logical local Y
        } else if(op==249 && src[i+1]==62) {
            add(249,{218});add(248,{218});
            add(128,{10,221,219,29});add(249,{216});
        } else {
            out.insert(out.end(),src.begin()+i,src.begin()+i+n);
        }
        i+=n;
    }
    return true;
}
}
