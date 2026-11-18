// Standalone regression for TLOU Qualcomm shader 0x4a705408 (ZIP 33).
// The real module is 6020 bytes; verify its exact 1D image-write pattern
// using the same instruction IDs without bundling proprietary game bytes.
#include "../gpu/shadps4/video_core/renderer_vulkan/vk_qualcomm_image1d_compat.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <vector>

using Vulkan::QualcommImage1DCompat::NarrowNonArrayed1DWrite;
using Vulkan::QualcommImage1DCompat::DropUnusedImageReadCapability;
using Words = std::vector<uint32_t>;
static void Add(Words& v,uint32_t op,std::initializer_list<uint32_t> p) {
    v.push_back((uint32_t(p.size()+1)<<16)|op);
    v.insert(v.end(),p.begin(),p.end());
}
int main() {
    Words source={0x07230203,0x00010600,0,0xd7,0};
    Add(source,17,{55});   // UNUSED StorageImageReadWithoutFormat
    Add(source,17,{56});   // REQUIRED StorageImageWriteWithoutFormat
    Add(source,21,{10,32,0});                    // OpTypeInt %10 32 0
    Add(source,23,{14,10,2});                    // OpTypeVector %14 %10 2
    Add(source,25,{56,8,1,0,0,0,2,0});          // image1D, nonarrayed, storage
    Add(source,61,{56,214,58});                 // OpLoad image %214
    Add(source,80,{14,211,89,90});              // vec2 coordinate %211=(%89,%90)
    Add(source,99,{214,211,213,0});             // OpImageWrite %214 %211 %213
    Words dropped, patched;
    assert(DropUnusedImageReadCapability(source,dropped));
    assert(dropped.size()+2==source.size());
    assert(dropped[5]==(2u<<16|17u) && dropped[6]==56u);
    assert(!DropUnusedImageReadCapability(dropped,patched));
    Words bad_read=source;
    Add(bad_read,98,{56,89}); // image read instruction means capability is required
    assert(!DropUnusedImageReadCapability(bad_read,patched));
    assert(NarrowNonArrayed1DWrite(source,patched));
    assert(source.size()==patched.size());
    size_t changes=0;
    for(size_t i=0;i<source.size();++i)
        if(source[i]!=patched[i]) {
            assert(source[i]==211 && patched[i]==89);
            ++changes;
        }
    assert(changes==1);
    Words rejected=source;
    // Arrayed image: second component is layer, cannot drop it.
    // Locate and toggle only the OpTypeImage Arrayed literal.
    for(size_t i=5;i<rejected.size();i+=rejected[i]>>16)
        if((rejected[i]&0xffff)==25) {rejected[i+5]=1;break;}
    assert(!NarrowNonArrayed1DWrite(rejected,patched));
    rejected=source;
    for(size_t i=5;i<rejected.size();i+=rejected[i]>>16)
        if((rejected[i]&0xffff)==25) {rejected[i+3]=2;break;}
    assert(!NarrowNonArrayed1DWrite(rejected,patched)); // 2D needs Y
    rejected=source;
    for(size_t i=5;i<rejected.size();i+=rejected[i]>>16)
        if((rejected[i]&0xffff)==99) {rejected[i+1]=215;break;}
    assert(!NarrowNonArrayed1DWrite(rejected,patched));
    rejected=source;
    rejected.back()=1; // nonzero image operands mask
    assert(!NarrowNonArrayed1DWrite(rejected,patched));
    std::puts("Qualcomm image capability/1D fallback: unused read capability removed, exactly one coordinate word changed");
}
