// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string_view>
#include <vector>

namespace Vulkan::QualcommTLOULegalize {
// Conservative fallback for the renderer's compute SPIR-V dialect. Refuse unknown
// instructions, non-32-bit atomics, narrow values that are actually used, or
// ambiguous LDS layouts. Never change the dispatch, descriptors or synchronization.
struct Stats {
    unsigned multiply{}, extended{}, lds_load{}, lds_store{}, barriers{}, atomics{};
};
struct Inst { uint32_t op; std::span<const uint32_t> a; uint32_t id{}, type{}; };
inline bool Typed(uint32_t op) {
    switch(op){
    case 1:case 12:case 41:case 42:case 43:case 44:case 54:case 59:
    case 61:case 65:case 79:case 80:case 81:case 83:case 86:case 88:case 95:
    case 109:case 112:case 113:case 124:case 127:case 128:case 129:case 130:
    case 131:case 132:case 133:case 136:case 151:case 166:case 167:case 168:
    case 169:case 170:case 171:case 172:case 174:case 176:case 178:case 184:
    case 186:case 188:case 194:case 196:case 199:case 202:case 203:
    case 234:case 237:case 239:case 245:return true;
    default:return false;
    }
}
inline bool Untyped(uint32_t op) {
    switch(op){case 7:case 11:case 19:case 20:case 21:case 22:case 23:case 25:
    case 26:case 27:case 28:case 29:case 30:case 32:case 33:case 248:return true;
    default:return false;}
}
// Operand classification for exactly the supported dialect. Literal indexes,
// image operand masks, names and decorations must not be interpreted as IDs.
template<class F> bool Refs(const Inst& r,F f){
    auto a=r.a;
    auto one=[&](size_t k){return k<a.size()&&f(a[k]);};
    auto rest=[&](size_t k){for(;k<a.size();++k)if(!f(a[k]))return false;return true;};
    if(Typed(r.op)&&!one(0))return false;
    switch(r.op){
    case 1:case 41:case 42:return a.size()==2;
    case 43:return a.size()>=3;
    case 5:case 6:case 16:return a.size()>=2&&one(0);
    case 7:case 10:case 11:case 14:case 17:case 19:case 20:case 21:case 22:
    case 26:case 56:case 248:case 253:return true;
    case 12:return a.size()>=4&&one(2)&&rest(4);
    case 15:return a.size()>=4&&a[0]==5&&a[2]==0x6e69616d&&a[3]==0&&one(1)&&rest(4);
    case 23:case 25:case 27:case 29:return one(1);
    case 28:case 30:case 33:return rest(1);
    case 32:return a.size()==3&&one(2);
    case 54:return a.size()==4&&one(3);
    case 59:return a.size()>=3&&a.size()<=4&&rest(3);
    case 61:return a.size()==3&&one(2);
    case 62:return a.size()==2&&rest(0);
    case 71:
        if(a.size()<2)return false;
        switch(a[1]){case 2:case 6:case 11:case 20:case 23:case 24:case 33:case 34:case 42:return one(0);default:return false;}
    case 72:return a.size()==4&&a[2]==35&&one(0);
    case 79:return a.size()>=5&&one(2)&&one(3);
    case 81:return a.size()>=4&&one(2);
    case 88:case 95:return a.size()>=5&&one(2)&&one(3)&&rest(5);
    case 99:return a.size()>=3&&one(0)&&one(1)&&one(2)&&rest(4);
    case 224:return a.size()==3&&rest(0);
    case 246:return a.size()==3&&one(0)&&one(1);
    case 247:return a.size()==2&&one(0);
    case 249:return a.size()==1&&one(0);
    case 250:return (a.size()==3||a.size()==5)&&one(0)&&one(1)&&one(2);
    default:return Typed(r.op)&&rest(2);
    }
}
struct Module {
    std::vector<Inst> rows;
    std::vector<const Inst*> defs;
    bool Parse(std::span<const uint32_t> src){
        if(src.size()<5||src.size()>1024*1024||src[0]!=0x07230203||src[1]!=0x00010600||
           src[2]!=0||src[3]<2||src[3]>262144||src[4]!=0)return false;
        for(size_t i=5;i<src.size();){
            uint32_t n=src[i]>>16,op=src[i]&65535u;
            if(!n||n>src.size()-i)return false;
            Inst r{op,src.subspan(i+1,n-1)};
            if(Typed(op)){if(r.a.size()<2)return false;r.type=r.a[0];r.id=r.a[1];}
            else if(Untyped(op)){if(r.a.empty())return false;r.id=r.a[0];}
            if((Typed(op)||Untyped(op))&&!r.id)return false;
            rows.push_back(r);i+=n;
        }
        defs.resize(src[3]);
        for(const auto& r:rows)if(r.id){if(r.id>=defs.size()||defs[r.id])return false;defs[r.id]=&r;}
        for(const auto& r:rows)if(!Refs(r,[&](uint32_t id){return id<defs.size()&&defs[id];}))return false;
        return true;
    }
    const Inst* Get(uint32_t id)const{return id<defs.size()?defs[id]:nullptr;}
    uint32_t Type(uint32_t id)const{const auto* d=Get(id);return d?d->type:0;}
};
inline bool NameIs(std::span<const uint32_t> words,std::string_view name){
    const size_t count=(name.size()+1+3)/4;
    if(words.size()!=count)return false;
    for(size_t i=0;i<count*4;++i){
        const uint8_t c=uint8_t(words[i/4]>>(8*(i%4)));
        if(c!=(i<name.size()?uint8_t(name[i]):0))return false;
    }
    return true;
}
inline bool Lower(std::span<const uint32_t> src,std::vector<uint32_t>& dst,Stats& stats){
    dst.clear();stats={};Module m;
    if(!m.Parse(src))return false;
    uint32_t u32=0,u64=0,v2=0,b1=0,zero=0,one=0;
    std::vector<bool> removed(src[3]);bool images=false;
    for(const auto& r:m.rows){auto a=r.a;
        if(r.op==21&&a.size()==3){
            if(a[1]==32&&a[2]==0){if(u32)return false;u32=r.id;}
            if(a[1]==64&&a[2]==0){if(u64)return false;u64=r.id;}
            if(a[1]==8||a[1]==16)removed[r.id]=true;
            else if(a[1]!=32&&!(a[1]==64&&a[2]==0))return false;
        }
        if(r.op==22&&(a.size()!=2||a[1]!=32))return false;
        if(r.op==17&&a.size()==1&&(a[0]==12||a[0]==4429))return false;
        if(r.op==20)b1=r.id;
        if(r.op==25)images=true;
    }
    if(!u32||!u64||!b1)return false;
    for(const auto& r:m.rows){auto a=r.a;
        if(r.op==23&&a.size()==3&&a[1]==u32&&a[2]==2)v2=r.id;
        if(r.op==43&&a.size()==3&&r.type==u32){if(a[2]==0)zero=r.id;if(a[2]==1)one=r.id;}
        if(r.op==43&&r.type&&removed[r.type])removed[r.id]=true;
    }
    if(!v2||!zero||!one)return false;
    struct Alias{uint32_t var{},ptr{},str{},array{},element{},elem_ptr{},words{};};
    Alias lo{},hi{};unsigned workgroups=0;
    auto layout=[&](const Inst& r,Alias& x){
        const auto* p=m.Get(r.type);if(!p||p->op!=32||p->a[1]!=4)return false;
        const auto* s=m.Get(p->a[2]);if(!s||s->op!=30||s->a.size()!=2)return false;
        const auto* a=m.Get(s->a[1]);if(!a||a->op!=28||a->a.size()!=3)return false;
        const auto* n=m.Get(a->a[2]);if(!n||n->op!=43||n->type!=u32||n->a.size()!=3||!n->a[2])return false;
        if(a->a[1]!=u32&&a->a[1]!=u64)return false;
        x={r.id,p->id,s->id,a->id,a->a[1],0,n->a[2]};
        for(const auto& q:m.rows)if(q.op==32&&q.a[1]==4&&q.a[2]==x.element){if(x.elem_ptr)return false;x.elem_ptr=q.id;}
        return x.elem_ptr!=0;
    };
    for(const auto& r:m.rows)if(r.op==59&&r.a[2]==4){
        ++workgroups;Alias x;
        if(!layout(r,x))continue;
        if(x.element==u64){if(hi.var)return false;hi=x;}else{if(lo.var)return false;lo=x;}
    }
    const bool alias=hi.var!=0;
    if(alias){
        if(workgroups!=2||!lo.var||uint64_t(lo.words)!=uint64_t(hi.words)*2||lo.words>65536)return false;
        auto decor=[&](uint32_t id,uint32_t d,uint32_t arg=0,bool member=false){
            for(const auto& r:m.rows){auto a=r.a;
                if(member&&r.op==72&&a.size()==4&&a[0]==id&&a[1]==0&&a[2]==d&&a[3]==arg)return true;
                if(!member&&r.op==71&&a.size()>=2&&a[0]==id&&a[1]==d&&(a.size()==2||a[2]==arg))return true;
            }return false;
        };
        if(!decor(lo.var,20)||!decor(hi.var,20)||!decor(lo.str,2)||!decor(hi.str,2)||
           !decor(lo.str,35,0,true)||!decor(hi.str,35,0,true)||!decor(lo.array,6,4)||!decor(hi.array,6,8))return false;
        for(const auto& r:m.rows){auto a=r.a;
            if(r.op==71){
                if((a[0]==lo.var||a[0]==hi.var)&&(a.size()!=2||a[1]!=20))return false;
                if((a[0]==lo.str||a[0]==hi.str)&&(a.size()!=2||a[1]!=2))return false;
                if((a[0]==lo.array||a[0]==hi.array)&&(a.size()!=3||a[1]!=6))return false;
            }
            if(r.op==72&&(a[0]==lo.str||a[0]==hi.str)&&(a[1]!=0||a[2]!=35||a[3]!=0))return false;
        }
        for(auto id:{hi.var,hi.ptr,hi.str,hi.array,hi.elem_ptr})removed[id]=true;
        for(const auto& r:m.rows)if(r.op==65&&r.type==hi.elem_ptr){
            if(r.a.size()!=5||r.a[2]!=hi.var||r.a[3]!=zero||m.Type(r.a[4])!=u32)return false;
            removed[r.id]=true;
        }
    }
    removed[u64]=true;
    std::vector<uint32_t> out(src.begin(),src.begin()+5);
    std::vector<std::array<uint32_t,2>> ptr(src[3]);
    uint32_t next=src[3],mask=next++,shift=next++,two=next++;
    auto emit=[&](uint32_t op,std::initializer_list<uint32_t> a){out.push_back((uint32_t(a.size()+1)<<16)|op);out.insert(out.end(),a.begin(),a.end());};
    auto copy=[&](const Inst& r){out.push_back((uint32_t(r.a.size()+1)<<16)|r.op);out.insert(out.end(),r.a.begin(),r.a.end());};
    auto bin=[&](uint32_t op,uint32_t a,uint32_t b){uint32_t id=next++;emit(op,{u32,id,a,b});return id;};
    auto extract=[&](uint32_t a,uint32_t k){uint32_t id=next++;emit(81,{u32,id,a,k});return id;};
    auto product=[&](uint32_t a,uint32_t b){
        uint32_t a0=bin(199,a,mask),a1=bin(194,a,shift),b0=bin(199,b,mask),b1=bin(194,b,shift);
        uint32_t p0=bin(132,a0,b0),p1=bin(132,a0,b1),p2=bin(132,a1,b0),p3=bin(132,a1,b1);
        uint32_t t=bin(128,bin(128,bin(194,p0,shift),bin(199,p1,mask)),bin(199,p2,mask));
        uint32_t h=bin(128,bin(128,bin(128,p3,bin(194,p1,shift)),bin(194,p2,shift)),bin(194,t,shift));
        return std::array<uint32_t,2>{bin(132,a,b),h};
    };
    auto iswide=[&](uint32_t id){return m.Type(id)==u64;};
    auto zeroextended=[&](uint32_t id){const auto* d=m.Get(id);return d&&d->op==113&&d->a.size()==3&&d->type==u64&&m.Type(d->a[2])==u32;};
    auto widezero=[&](uint32_t id){const auto* d=m.Get(id);return d&&d->op==43&&d->type==u64&&d->a.size()==4&&d->a[2]==0&&d->a[3]==0;};
    bool constants=false;unsigned entry=0;Stats s{};
    for(const auto& r:m.rows){auto a=r.a;
        if(r.op==17&&a.size()==1){
            switch(a[0]){
            case 11:case 22:case 39:case 4433:case 4448:continue;
            case 4428:case 4430:if(alias)continue;break;
            case 43:case 44:case 49:case 50:case 55:case 56:if(!images)continue;break;
            default:break;
            }
        }
        if(alias&&r.op==10&&NameIs(a,"SPV_KHR_workgroup_memory_explicit_layout"))continue;
        if(r.op==5||r.op==6||r.op==71||r.op==72){
            if(removed[a[0]])continue;
            if(alias&&(r.op==71||r.op==72)&&(a[0]==lo.var||a[0]==lo.str||a[0]==lo.array))continue;
            if((r.op==5||r.op==6)&&iswide(a[0])){copy(r);continue;}
        }
        if(r.op==15){
            ++entry;
            if(alias){
                if(std::count(a.begin()+4,a.end(),hi.var)!=1)return false;
                out.push_back((uint32_t(a.size())<<16)|15u);
                for(size_t k=0;k<a.size();++k)if(k<4||a[k]!=hi.var)out.push_back(a[k]);
                continue;
            }
        }
        if(r.op==54&&!constants){emit(43,{u32,mask,65535});emit(43,{u32,shift,16});emit(43,{u32,two,2});constants=true;}
        if(r.op==43&&r.type==u64){
            if(a.size()!=4||a[2]>1||a[3]!=0)return false;
            // The original generator declares these scalar constants first.
            if(m.Get(zero)> &r || m.Get(one)> &r)return false;
            emit(44,{v2,r.id,a[2]?one:zero,zero});continue;
        }
        if(r.op==65&&alias&&r.type==hi.elem_ptr){
            uint32_t idx=bin(132,a[4],two),high=bin(128,idx,one),p0=next++,p1=next++;
            emit(65,{lo.elem_ptr,p0,lo.var,zero,idx});emit(65,{lo.elem_ptr,p1,lo.var,zero,high});ptr[r.id]={p0,p1};continue;
        }
        if(r.op==61&&r.type==u64){
            if(!alias||!ptr[a[2]][0])return false;
            auto p=ptr[a[2]];uint32_t l=next++,h=next++;
            emit(61,{u32,l,p[0]});emit(61,{u32,h,p[1]});emit(80,{v2,r.id,l,h});++s.lds_load;continue;
        }
        if(r.op==62&&alias&&ptr[a[0]][0]){
            if(!iswide(a[1]))return false;
            auto p=ptr[a[0]];uint32_t l=extract(a[1],0),h=extract(a[1],1);
            emit(62,{p[0],l});emit(62,{p[1],h});++s.lds_store;continue;
        }
        if(r.op==124&&a.size()==3&&(r.type==u64||iswide(a[2]))){
            if(!((r.type==u64&&m.Type(a[2])==v2)||(r.type==v2&&iswide(a[2]))))return false;
            emit(83,{v2,r.id,a[2]});continue;
        }
        if(r.op==113&&r.type==u64){
            if(a.size()!=3||m.Type(a[2])!=u32)return false;
            emit(80,{v2,r.id,a[2],zero});continue;
        }
        if(r.op==132&&r.type==u64){
            if(a.size()!=4||!zeroextended(a[2])||!zeroextended(a[3]))return false;
            auto p=product(m.Get(a[2])->a[2],m.Get(a[3])->a[2]);emit(80,{v2,r.id,p[0],p[1]});++s.multiply;continue;
        }
        if(r.op==128&&r.type==u64){
            if(a.size()!=4||!iswide(a[2])||!iswide(a[3]))return false;
            if(widezero(a[3])){emit(83,{v2,r.id,a[2]});continue;}
            if(widezero(a[2])){emit(83,{v2,r.id,a[3]});continue;}
            uint32_t al=extract(a[2],0),ah=extract(a[2],1),bl=extract(a[3],0),bh=extract(a[3],1);
            uint32_t l=bin(128,al,bl),cmp=next++,carry=next++;
            emit(176,{b1,cmp,l,al});emit(169,{u32,carry,cmp,one,zero});
            uint32_t h=bin(128,bin(128,ah,bh),carry);emit(80,{v2,r.id,l,h});continue;
        }
        if(r.op==151){
            const auto* t=m.Get(r.type);
            if(a.size()!=4||!t||t->op!=30||t->a.size()!=3||t->a[1]!=u32||t->a[2]!=u32||m.Type(a[2])!=u32||m.Type(a[3])!=u32)return false;
            auto p=product(a[2],a[3]);emit(80,{r.type,r.id,p[0],p[1]});++s.extended;continue;
        }
        if(r.id&&removed[r.id])continue;
        if(!Refs(r,[&](uint32_t id){return !removed[id]&&!iswide(id);}))return false;
        if(r.op==224)++s.barriers;
        if(r.op==234||r.op==237||r.op==239)++s.atomics;
        copy(r);
    }
    if(entry!=1||!constants||!(s.multiply||s.extended||s.lds_load||s.lds_store))return false;
    out[3]=next;Module check;if(!check.Parse(out))return false;
    stats=s;dst=std::move(out);return true;
}
}
