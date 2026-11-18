// Standalone unit tests; no Vulkan SDK or device needed.
#include "../gpu/shadps4/video_core/renderer_vulkan/vk_qualcomm_float_compat.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
using Vulkan::QualcommFloatCompat::DropExplicitFloatControls;
using Words = std::vector<uint32_t>;
static void Add(Words& v, uint32_t op, std::initializer_list<uint32_t> args) {
    v.push_back((uint32_t(args.size() + 1) << 16) | op);
    v.insert(v.end(), args.begin(), args.end());
}
static void AddFloatExtension(Words& v) {
    constexpr char text[] = "SPV_KHR_float_controls";
    constexpr size_t words = (sizeof(text) + 3) / 4;
    uint32_t body[words]{};
    std::memcpy(body, text, sizeof(text));
    v.push_back(uint32_t((words + 1) << 16) | 10);
    v.insert(v.end(), body, body + words);
}
static bool Has(const Words& v, uint32_t op, uint32_t arg) {
    for(size_t i = 5; i < v.size();) {
        const uint32_t n = v[i] >> 16;
        if (!n || n > v.size()-i) return false;
        if ((v[i] & 0xffff) == op && n >= 2 && v[i+1] == arg) return true;
        i += n;
    }
    return false;
}
int main() {
    // Exact capability/execution-mode instruction pattern of ZIP32 compute
    // shader 0x4a705408; preserve workgroup=32x32x1 and Image1D capability.
    Words src={0x07230203,0x00010600,0,0xd7,0};
    Add(src,17,{1});
    Add(src,17,{44});       // Image1D; do not remove
    Add(src,17,{4465});     // DenormFlushToZero
    Add(src,17,{4466});     // SignedZeroInfNanPreserve
    AddFloatExtension(src);
    Add(src,14,{0,1});
    Add(src,16,{60,17,32,32,1});
    Add(src,16,{60,4460,32});
    Add(src,16,{60,4461,32});
    Add(src,19,{1});
    Words variant;
    assert(DropExplicitFloatControls(src,variant));
    assert(variant.size() < src.size());
    assert(std::equal(src.begin(),src.begin()+5,variant.begin()));
    assert(Has(variant,17,1) && Has(variant,17,44));
    assert(!Has(variant,17,4465) && !Has(variant,17,4466));
    assert(Has(variant,16,60));   // local size remains unchanged
    assert(variant[variant.size()-2] == 0x20013 && variant.back() == 1);
    Words second;
    assert(!DropExplicitFloatControls(variant,second)); // no flags left
    Words malformed=src;
    malformed[5]=0;
    assert(!DropExplicitFloatControls(malformed,variant));
    Words unsupported=src;
    unsupported.insert(unsupported.begin()+5,{0x20011,4464});
    assert(!DropExplicitFloatControls(unsupported,variant));
    std::puts("Qualcomm float mode fallback: valid pattern, unchanged workgroup, unsupported modes rejected");
}
