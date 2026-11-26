// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <utility>
#include <cstdint>
#include <span>
#include <vector>

namespace QualcommDeadIntDecls {
using Word=std::uint32_t;
constexpr std::array<Word,10> DeadIds{4,5,6,7,11,26,27,28,32,33};
inline bool Dead(Word id) {
    for(auto dead:DeadIds)if(id==dead)return true;
    return false;
}
inline bool RemovedCapability(Word cap) {
    return cap==11 || cap==22 || cap==39 || cap==4433 || cap==4448;
}
// Recognize the Sirit emitter's exact unused integer declarations. We do not
// infer liveness from a shader hash. Any retained possible reference rejects
// the transform. Only documented literal operand positions may be ignored.
inline bool LiteralOperand(Word op,std::span<const Word> a,std::size_t j) {
    switch(op) {
    case 5: return j>=1; // Name string
    case 6: return j>=1; // MemberName member number and string
    case 10: return true; // Extension string
    case 14: case 17: return true; // MemoryModel, Capability
    case 15: { // EntryPoint model, function ID, null-terminated name, interface IDs
        if(j==0)return true;
        if(j==1)return false;
        for(std::size_t i=2;i<a.size();++i) {
            const Word v=a[i];
            if(i==j)return true;
            if(!(v&255u)||!((v>>8)&255u)||!((v>>16)&255u)||!((v>>24)&255u))break;
        }
        return false;
    }
    case 16: return j==1 || (j>=2&&a.size()>=2 && (a[1]==7||a[1]==12||a[1]==4460||a[1]==4461));
    case 21: case 22: return j>=1; // Integer/float width and signedness
    case 23: case 24: return j==2; // Vector/matrix component count
    case 25: return j>=2; // TypeImage enum operands
    case 32: return j==1; // TypePointer storage class
    case 43: return j>=2; // Constant literal words
    case 12: return j==3; // ExtInst instruction number
    case 54: return j==2; // Function control
    case 59: return j==2; // Variable storage class
    case 71: // Decorate: only whitelist decorations with literal arguments
        if(j==1)return true;
        return j>=2 && a.size()>=2 &&
            (a[1]==2||a[1]==6||a[1]==11||a[1]==17||a[1]==24||
             a[1]==30||a[1]==33||a[1]==34||a[1]==42);
    case 72: // MemberDecorate, literal member and decoration enum
        return j==1||j==2||(j>=3&&a.size()>=3&&a[2]==35);
    case 79: return j>=4; // VectorShuffle indices
    case 81: return j>=3; // CompositeExtract indices
    case 82: return j>=4; // CompositeInsert indices
    default:return false;
    }
}
inline bool Strip(std::span<const Word> src,std::vector<Word>& dst) {
    dst.clear();
    if(src.size()<5||src[0]!=0x07230203u||src[3]<=33)return false;
    std::array<bool,10> seen{};
    std::vector<Word> out(src.begin(),src.begin()+5);
    unsigned removed=0;
    for(std::size_t i=5;i<src.size();) {
        const Word n=src[i]>>16,op=src[i]&65535u;
        if(!n||n>src.size()-i)return false;
        auto a=src.subspan(i+1,n-1);
        bool skip=false;
        int decl=-1;
        if(op==21) {
            if(a.size()!=3)return false;
            if(a[1]!=32) {
                for(unsigned k=0;k<5;++k)if(a[0]==DeadIds[k])decl=int(k);
                if(decl<0)return false;
                constexpr Word widths[]{8,8,16,16,64};
                constexpr Word signs[]{0,1,0,1,0};
                if(a[1]!=widths[decl]||a[2]!=signs[decl])return false;
                skip=true;
            }
        } else if(op==22) {
            // StorageBuffer16BitAccess can also authorize float16. Do not
            // remove it from a module declaring any non-32-bit float type.
            if(a.size()!=2||a[1]!=32)return false;
        } else if(op==43 && a.size()>=3 && Dead(a[1])) {
            for(unsigned k=5;k<10;++k)if(a[1]==DeadIds[k])decl=int(k);
            if(decl<5)return false;
            constexpr Word types[]{4,4,6,11,11};
            constexpr Word vals[]{1,0,0,1,0};
            const unsigned k=unsigned(decl)-5;
            if(a[0]!=types[k]||a[2]!=vals[k]||
               a.size()!=(k>=3?4u:3u)||(k>=3&&a[3]!=0))return false;
            skip=true;
        } else if(op==5 && !a.empty() && Dead(a[0])) {
            skip=true;
        } else if(op==17&&a.size()==1&&RemovedCapability(a[0])) {
            skip=true;
        }
        if(decl>=0) {
            if(seen[decl])return false;
            seen[decl]=true;
        }
        if(skip)++removed;
        else {
            for(std::size_t j=0;j<a.size();++j)
                if(Dead(a[j])&&!LiteralOperand(op,a,j))return false;
            out.insert(out.end(),src.begin()+i,src.begin()+i+n);
        }
        i+=n;
    }
    for(bool present:seen)if(!present)return false;
    if(!removed)return false;
    dst=std::move(out);
    return true;
}
// Two captured vertex modules declare unused narrow aliases of live
// 32-bit storage bindings. The complete executable stream has no reference
// to their variables or types. Authorize only the exact captured byte layout;
// reject any remaining reference, and retain descriptor layout on the host.
inline bool StripCapturedStorageAliases(std::span<const Word> src,std::vector<Word>& dst) {
    dst.clear();
    if(src.size()<5||src[0]!=0x07230203u)return false;
    std::uint64_t hash=14695981039346656037ull;
    for(Word w:src)for(unsigned shift=0;shift<32;shift+=8)
        hash=(hash^((w>>shift)&255u))*1099511628211ull;
    constexpr Word first[]{65,66,67,68,69,70,71,72,73,74,77,78,81,82,85,86,89,90};
    constexpr Word second[]{62,63,64,65,66,67,68,69,70,71,74,75};
    std::span<const Word> ids;
    if(src.size()==4102&&src[3]==587&&hash==0x61a2e976e7234fa1ull)ids=first;
    else if(src.size()==2440&&src[3]==350&&hash==0xa4154aff75f158a6ull)ids=second;
    else return false;
    const auto dead=[&](Word w){for(Word id:ids)if(id==w)return true;return false;};
    std::vector<Word> out(src.begin(),src.begin()+5);
    unsigned removed=0;
    for(std::size_t i=5;i<src.size();) {
        const Word n=src[i]>>16,op=src[i]&65535u;
        if(!n||n>src.size()-i)return false;
        const auto a=src.subspan(i+1,n-1);
        bool skip=false;
        if((op==5||op==6||op==71||op==72||op==29||op==30||op==32)&&!a.empty()&&dead(a[0]))
            skip=true;
        if(op==59&&a.size()==3&&dead(a[1])&&dead(a[0])&&a[2]==12)skip=true;
        if(skip){++removed;i+=n;continue;}
        if(op==15) {
            // Both exact captures have entry-point name "main" in two words.
            if(a.size()<4||a[2]!=0x6e69616du||a[3]!=0)return false;
            const auto start=out.size();
            out.push_back(0);
            out.insert(out.end(),a.begin(),a.begin()+4);
            for(std::size_t j=4;j<a.size();++j)if(!dead(a[j]))out.push_back(a[j]);
            out[start]=(Word(out.size()-start)<<16)|op;
        } else {
            for(std::size_t j=0;j<a.size();++j)
                if(dead(a[j])&&!LiteralOperand(op,a,j))return false;
            out.insert(out.end(),src.begin()+i,src.begin()+i+n);
        }
        i+=n;
    }
    if(!removed)return false;
    return Strip(out,dst);
}
} // namespace QualcommDeadIntDecls
