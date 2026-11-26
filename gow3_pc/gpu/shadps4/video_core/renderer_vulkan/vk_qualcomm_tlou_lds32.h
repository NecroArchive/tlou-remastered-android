// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <cstddef>
#include <span>
#include <vector>
#include <initializer_list>
#include <array>
namespace Vulkan::QualcommTLOULds32 {
// 0x71859bbb: the captured shader aliases u32[2176] and u64[1088]
// over the same 8704 bytes of LDS. No 64-bit atomics or arithmetic:
// 69 wide accesses, 42 loads, 27 stores, each bitcast to/from uvec2.
// Use one ordinary Workgroup array of u32. Keep all barriers and atomics.
constexpr std::array<uint32_t,2> WordIndices(uint32_t q) {return {q*2,q*2+1};}
inline bool Lower(std::span<const uint32_t> src,std::vector<uint32_t>& dst) {
    dst.clear();
    if(src.size()!=8072 || src[0]!=0x07230203 || src[3]!=1372)return false;
    uint64_t h=14695981039346656037ull;
    for(uint32_t w:src)for(unsigned s=0;s<32;s+=8)
        h=(h^((w>>s)&255u))*1099511628211ull;
    if(h!=0xbecbb72853bffd4cull)return false;
    std::vector<uint32_t> out(src.begin(),src.begin()+5);
    uint32_t next=src[3];
    const uint32_t two=next++;
    std::array<std::array<uint32_t,2>,1372> ptr{};
    unsigned pairs=0,loads=0,stores=0,casts=0,barriers=0,atomics=0;
    auto emit=[&](uint32_t op,std::initializer_list<uint32_t> a){
        out.push_back((uint32_t(a.size()+1)<<16)|op);
        out.insert(out.end(),a.begin(),a.end());
    };
    auto removed=[](uint32_t id){
        return id==4||id==5||id==6||id==7||id==11||
               id==26||id==27||id==28||id==32||id==33||
               (id>=56&&id<=61);
    };
    for(size_t i=5;i<src.size();) {
        uint32_t n=src[i]>>16,op=src[i]&65535u;
        if(!n||n>src.size()-i)return false;
        auto a=src.subspan(i+1,n-1);bool skip=false;
        if(op==17&&n==2){
            // Int64, unused narrow types, unused image features and
            // WorkgroupMemoryExplicitLayoutKHR / 16-bit layout capability.
            switch(a[0]){case 11:case 22:case 39:case 43:case 44:case 49:
            case 50:case 55:case 4433:case 4448:case 4428:case 4430:skip=true;break;default:break;}
        }else if(op==10&&i==44)skip=true; // exact explicit-layout extension
        else if((op==5||op==6||op==71||op==72)&&n>=2){
            skip=removed(a[0])||((a[0]==51||a[0]==52||a[0]==55)&&(op==71||op==72));
        }else if(op==15&&n==17){
            // Remove only the unused 64-bit alias from the entry interface.
            out.push_back((uint32_t(n-1)<<16)|op);
            for(size_t k=0;k<a.size();++k)if(k<4||a[k]!=61)out.push_back(a[k]);
            skip=true;
        }else if((op==21||op==28||op==30||op==32)&&n>=2&&removed(a[0]))skip=true;
        else if((op==43||op==59)&&n>=3&&removed(a[1]))skip=true;
        if(skip){i+=n;continue;}
        if(op==54)emit(43,{10,two,2});
        if(op==65&&n==6&&a[0]==60&&a[2]==61&&a[3]==30&&a[1]<ptr.size()){
            uint32_t idx=next++,hi=next++,p0=next++,p1=next++;
            emit(132,{10,idx,a[4],two});
            emit(128,{10,hi,idx,29}); // constant 1
            emit(65,{54,p0,55,30,idx});emit(65,{54,p1,55,30,hi});
            ptr[a[1]]={p0,p1};++pairs;
        }else if(op==124&&n==4&&a[0]==11){
            emit(83,{14,a[1],a[2]});++casts;
        }else if(op==124&&n==4&&a[0]==14){
            // All vec2 bitcasts in the captured module must be checked:
            // only the result of a former u64 load needs CopyObject.
            bool wide=false;
            for(size_t p=5;p<i;){uint32_t m=src[p]>>16;
                if((src[p]&65535u)==61&&m==4&&src[p+1]==11&&src[p+2]==a[2]){wide=true;break;}
                p+=m;}
            if(wide){emit(83,{14,a[1],a[2]});++casts;}
            else out.insert(out.end(),src.begin()+i,src.begin()+i+n);
        }else if(op==61&&n==4&&a[0]==11&&a[2]<ptr.size()&&ptr[a[2]][0]){
            uint32_t low=next++,high=next++;
            emit(61,{10,low,ptr[a[2]][0]});emit(61,{10,high,ptr[a[2]][1]});
            emit(80,{14,a[1],low,high});++loads;
        }else if(op==62&&n==3&&a[0]<ptr.size()&&ptr[a[0]][0]){
            uint32_t low=next++,high=next++;
            emit(81,{10,low,a[1],0});emit(81,{10,high,a[1],1});
            emit(62,{ptr[a[0]][0],low});emit(62,{ptr[a[0]][1],high});++stores;
        }else{
            if(op==224)++barriers;
            if(op==234||op==237||op==239)++atomics;
            out.insert(out.end(),src.begin()+i,src.begin()+i+n);
        }
        i+=n;
    }
    if(pairs!=69||loads!=42||stores!=27||casts!=69||barriers!=7||atomics!=3)return false;
    out[3]=next;dst=std::move(out);return true;
}
}
