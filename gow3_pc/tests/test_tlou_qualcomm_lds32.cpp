#include "../gpu/shadps4/video_core/renderer_vulkan/vk_qualcomm_tlou_lds32.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
int main(int argc,char** argv){
    using namespace Vulkan::QualcommTLOULds32;
    for(uint32_t i=0;i<1088;++i){
        auto p=WordIndices(i);assert(p[0]*4==i*8&&p[1]*4==i*8+4&&p[1]<2176);
    }
    std::vector<uint32_t> fake(8072),out;
    fake[0]=0x07230203;fake[3]=1372;
    assert(!Lower(fake,out)&&out.empty());
    if(argc==3){
        std::ifstream f(argv[1],std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(f)),{});
        assert(bytes.size()==8072*4);
        std::vector<uint32_t> words(8072);
        for(size_t i=0;i<words.size();++i)for(unsigned j=0;j<4;++j)
            words[i]|=uint32_t(uint8_t(bytes[i*4+j]))<<(j*8);
        assert(Lower(words,out));
        std::ofstream g(argv[2],std::ios::binary);
        g.write(reinterpret_cast<const char*>(out.data()),out.size()*4);
        assert(g.good());
    }
    std::puts("Qualcomm LDS32: all 1088 alias positions and exact-capture guard passed");
}
