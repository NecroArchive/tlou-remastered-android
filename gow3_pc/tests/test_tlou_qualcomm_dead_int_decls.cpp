// SPDX-License-Identifier: GPL-2.0-or-later
#include "../gpu/shadps4/video_core/renderer_vulkan/tlou_qualcomm_dead_int_decls.h"
#include <cassert>
#include <initializer_list>
using namespace QualcommDeadIntDecls;
static void Emit(std::vector<Word>& out,Word op,std::initializer_list<Word> operands) {
    out.push_back((Word(operands.size()+1)<<16)|op);
    out.insert(out.end(),operands.begin(),operands.end());
}
static std::vector<Word> Fixture() {
    std::vector<Word> w{0x07230203,0x00010600,0,128,0};
    for(Word cap:{11u,22u,39u,4433u,4448u,1u})Emit(w,17,{cap});
    Emit(w,14,{0,1});
    Emit(w,15,{4,50,0x6e69616d,0,51});
    Emit(w,16,{50,7});
    Emit(w,5,{4,0x003875});
    Emit(w,71,{51,33,4}); // binding literal equals a removed ID
    Emit(w,21,{4,8,0});Emit(w,21,{5,8,1});
    Emit(w,21,{6,16,0});Emit(w,21,{7,16,1});Emit(w,21,{11,64,0});
    Emit(w,21,{10,32,0});Emit(w,22,{8,32});
    Emit(w,43,{4,26,1});Emit(w,43,{4,27,0});Emit(w,43,{6,28,0});
    Emit(w,43,{11,32,1,0});Emit(w,43,{11,33,0,0});
    Emit(w,43,{10,61,32}); // constant literal equals a removed ID
    Emit(w,54,{10,50,0,62});
    Emit(w,248,{63});
    Emit(w,128,{10,64,61,61});
    Emit(w,253,{});Emit(w,56,{});
    return w;
}
int main() {
    auto src=Fixture();std::vector<Word> out;
    assert(Strip(src,out));
    assert(out.size()<src.size());
    for(std::size_t i=5;i<out.size();i+=out[i]>>16) {
        const Word op=out[i]&65535;
        if(op==21)assert(out[i+2]==32);
        if(op==17)assert(!RemovedCapability(out[i+1]));
    }
    auto live=src;Emit(live,124,{11,70,61}); // actual wide result type
    assert(!Strip(live,out)&&out.empty());
    live=src;Emit(live,128,{10,70,26,61}); // actual constant operand
    assert(!Strip(live,out)&&out.empty());
    live=src;Emit(live,22,{73,16}); // storage16 capability may be live
    assert(!Strip(live,out)&&out.empty());
    live=src;Emit(live,32,{74,12,4}); // actual narrow pointee
    assert(!Strip(live,out)&&out.empty());
    live=src;Emit(live,71,{51,12345,26}); // unknown decoration argument: conservative reject
    assert(!Strip(live,out)&&out.empty());
    live=src;live.push_back(0);assert(!Strip(live,out)&&out.empty());
}
