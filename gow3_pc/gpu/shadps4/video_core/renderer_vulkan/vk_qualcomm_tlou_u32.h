// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <span>
#include <vector>
#include <initializer_list>
#include "vk_qualcomm_tlou_workgroup.h"

namespace Vulkan::QualcommTLOUU32 {
// Exact capture 0x4a705408. The guest's multiply-high/low results can
// be produced from four 16-bit partial products without any wide ALU.
// This also avoids the proprietary compiler's OpUMulExtended lowering.
constexpr uint32_t High(uint32_t a,uint32_t b) {
    uint32_t a0=a&65535u,a1=a>>16,b0=b&65535u,b1=b>>16;
    uint32_t p0=a0*b0,p1=a0*b1,p2=a1*b0,p3=a1*b1;
    uint32_t t=(p0>>16)+(p1&65535u)+(p2&65535u);
    return p3+(p1>>16)+(p2>>16)+(t>>16);
}
inline bool Lower(std::span<const uint32_t> src,std::vector<uint32_t>& out,bool small=false) {
    out.clear();
    if(src.size()!=1505 || src[0]!=0x07230203 || src[3]!=215)return false;
    uint64_t h=14695981039346656037ull;
    for(uint32_t x:src)for(unsigned shift=0;shift<32;shift+=8)
        h=(h^((x>>shift)&255u))*1099511628211ull;
    if(h!=0xb2ac7871d66934aaull)return false;
    std::vector<uint32_t> reduced;
    if(small){
        if(!QualcommTLOUWorkgroup::Reduce(src,reduced))return false;
        src=reduced;
    }
    out.assign(src.begin(),src.begin()+5);
    uint32_t next=src[3];
    auto add=[&](uint32_t op,std::initializer_list<uint32_t> args){
        out.push_back((uint32_t(args.size()+1)<<16)|op);
        out.insert(out.end(),args.begin(),args.end());
    };
    const uint32_t mask=next++,shift=next++;
    auto bin=[&](uint32_t op,uint32_t a,uint32_t b){
        uint32_t result=next++;add(op,{10,result,a,b});return result;
    };
    auto product=[&](uint32_t a,uint32_t b,uint32_t low,uint32_t high){
        uint32_t a0=bin(199,a,mask),a1=bin(194,a,shift);
        uint32_t b0=bin(199,b,mask),b1=bin(194,b,shift);
        uint32_t p0=bin(132,a0,b0),p1=bin(132,a0,b1);
        uint32_t p2=bin(132,a1,b0),p3=bin(132,a1,b1);
        uint32_t c0=bin(194,p0,shift),c1=bin(199,p1,mask),c2=bin(199,p2,mask);
        uint32_t t0=bin(128,c0,c1),t=bin(128,t0,c2);
        uint32_t hi1=bin(194,p1,shift),hi2=bin(194,p2,shift),hi3=bin(194,t,shift);
        uint32_t h0=bin(128,p3,hi1),h1=bin(128,h0,hi2);
        add(128,{10,high,h1,hi3});add(132,{10,low,a,b});
    };
    unsigned replaced=0,wide=0;
    for(size_t i=5;i<src.size();){
        const uint32_t n=src[i]>>16,op=src[i]&65535u;
        if(!n||n>src.size()-i){out.clear();return false;}
        // Unused 8/16-bit types, 64-bit type and dead constants. The exact
        // hash guarantees the remaining instructions have no references.
        bool skip=false;
        if(op==17 && n==2){
            switch(src[i+1]){
                case 11:case 22:case 39:case 43:case 44:case 49:case 50:
                case 55:case 4433:case 4448:skip=true;break;
                default:break; // preserve FP execution modes and Shader/ImageWrite
            }
        }else if((op==21||op==5) && n>=3){
            const uint32_t id=src[i+1];
            skip=(id>=4&&id<=7)||id==11;
        }else if(op==43 && n>=4){
            const uint32_t id=src[i+2];
            skip=(id>=26&&id<=28)||id==32||id==33;
        }
        if(skip){i+=n;continue;}
        if(op==54){
            add(43,{10,mask,65535u});add(43,{10,shift,16u});
        }
        if((op==113||op==132||op==128||op==124) && n>=4 &&
            src[i+2]>=98&&src[i+2]<=102){
            ++wide;i+=n;continue;
        }
        if(op==81 && n==5 && src[i+2]==103){
            product(85,96,103,104);i+=n;continue;
        }
        if(op==81 && n==5 && src[i+2]==104){i+=n;continue;}
        if(op==151 && n==5){
            uint32_t low=next++,high=next++;
            product(src[i+3],src[i+4],low,high);
            add(80,{src[i+1],src[i+2],low,high});
            ++replaced;i+=n;continue;
        }
        out.insert(out.end(),src.begin()+i,src.begin()+i+n);i+=n;
    }
    if(wide!=5||replaced!=3){out.clear();return false;}
    out[3]=next;
    return true;
}
}
