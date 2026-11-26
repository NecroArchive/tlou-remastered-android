#include "../gpu/shadps4/video_core/renderer_vulkan/vk_qualcomm_tlou_u32.h"
#include <cassert>
#include <cstdio>
int main(){
    using namespace Vulkan::QualcommTLOUU32;
    uint32_t r=0x631bf001u;
    for(unsigned i=0;i<200000;++i){
        r=r*1664525u+1013904223u;uint32_t a=r;
        r=r*1664525u+1013904223u;uint32_t b=r;
        assert(High(a,b)==uint32_t((uint64_t(a)*b)>>32));
    }
    assert(High(0xffffffffu,0xffffffffu)==0xfffffffeu);
    assert(High(0,0xffffffffu)==0);
    std::vector<uint32_t> fake(1505),out;
    fake[0]=0x07230203;fake[3]=215;
    assert(!Lower(fake,out)&&out.empty());
    std::puts("Qualcomm exact u32 partial products: 200002 cases and hash rejection passed");
}
