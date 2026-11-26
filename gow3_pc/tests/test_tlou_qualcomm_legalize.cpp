// SPDX-License-Identifier: GPL-2.0-or-later
#include "../gpu/shadps4/video_core/renderer_vulkan/vk_qualcomm_tlou_legalize.h"
#include <cassert>
#include <cstdio>
using namespace Vulkan::QualcommTLOULegalize;
static std::vector<uint32_t> Fixture(bool wide_atomic=false,bool bad_layout=false){
    std::vector<uint32_t> s{0x07230203,0x00010600,0,64,0};
    auto op=[&](uint32_t code,std::initializer_list<uint32_t> a){
        s.push_back((uint32_t(a.size()+1)<<16)|code);s.insert(s.end(),a.begin(),a.end());
    };
    op(17,{1});op(17,{11});op(17,{4428});op(14,{0,1});
    op(15,{5,25,0x6e69616d,0,20,21});op(16,{25,17,1,1,1});
    op(71,{11,6,4});op(71,{12,6,8});op(71,{13,2});op(71,{14,2});
    op(72,{13,0,35,0});op(72,{14,0,35,0});op(71,{20,20});op(71,{21,20});
    op(19,{1});op(20,{2});op(21,{3,32,0});op(21,{4,64,0});op(23,{5,3,2});op(30,{6,3,3});
    op(43,{3,7,0});op(43,{3,8,1});op(43,{3,9,8});op(43,{3,10,bad_layout?3u:4u});
    op(28,{11,3,9});op(28,{12,4,10});op(30,{13,11});op(30,{14,12});
    op(32,{15,4,13});op(32,{16,4,14});op(32,{17,4,3});op(32,{18,4,4});
    op(33,{19,1});op(59,{15,20,4});op(59,{16,21,4});op(43,{4,22,0,0});
    op(43,{3,23,2});op(43,{3,24,264});
    op(54,{1,25,0,19});op(248,{26});
    op(113,{4,27,8});op(113,{4,28,9});op(132,{4,29,27,28});
    op(128,{4,30,29,22});op(151,{6,31,8,9});
    op(65,{18,32,21,7,8});op(62,{32,30});
    op(224,{23,23,24});op(61,{4,33,32});
    op(124,{5,34,33});op(65,{17,35,20,7,8});
    if(wide_atomic)op(234,{4,36,32,23,7,33});else op(234,{3,36,35,23,7,8});
    op(253,{});op(56,{});
    return s;
}
int main(){
    std::vector<uint32_t> out;Stats s;
    auto fixture=Fixture();assert(Lower(fixture,out,s));
    assert(s.multiply==1&&s.extended==1&&s.lds_load==1&&s.lds_store==1&&s.barriers==1&&s.atomics==1);
    Module check;assert(check.Parse(out));
    for(const auto& r:check.rows){
        assert(!(r.op==21&&r.a[1]!=32));
        assert(r.op!=151);
        assert(!(r.op==17&&(r.a[0]==11||r.a[0]==4428)));
    }
    assert(!Lower(Fixture(true),out,s)&&out.empty());
    assert(!Lower(Fixture(false,true),out,s)&&out.empty());
    fixture.push_back((1u<<16)|999u);
    assert(!Lower(fixture,out,s)&&out.empty());
    std::puts("Qualcomm structural legalizer: combined multiply/LDS, atomics and rejection guards passed");
}
