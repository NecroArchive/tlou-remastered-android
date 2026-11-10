// SPDX-License-Identifier: GPL-2.0-or-later
// Exact production transfer preconditions for the PC v0.1.7 TLOU fix.
#include "video_core/renderer_vulkan/tlou_large_scalar_copy.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <random>

using Vulkan::Detail::EligibleLargeScalarCopy;
using Vulkan::Detail::NonOverlappingVulkanCopy;
static constexpr std::uint64_t SHADER=0x82c46b8bull;
static_assert(EligibleLargeScalarCopy(SHADER,SHADER,32768,32768,0x100000,0x200000,131072));
static_assert(!EligibleLargeScalarCopy(SHADER+1,SHADER,32768,32768,0x100000,0x200000,131072));
static_assert(!EligibleLargeScalarCopy(SHADER,SHADER,32768,32767,0x100000,0x200000,131072));
static_assert(!EligibleLargeScalarCopy(SHADER,SHADER,16384,16384,0x100000,0x200000,65536));
static_assert(!EligibleLargeScalarCopy(SHADER,SHADER,32768,32768,0x100000,0x100001,131072));
static_assert(!EligibleLargeScalarCopy(SHADER,SHADER,32768,32768,0x100000,0x100000,131072));
static_assert(!EligibleLargeScalarCopy(SHADER,SHADER,32768,32768,0x100000,
                                       std::numeric_limits<std::uint64_t>::max()-8,131072));
static_assert(NonOverlappingVulkanCopy(true,0,65536,65536));
static_assert(!NonOverlappingVulkanCopy(true,0,65535,65536));
static_assert(NonOverlappingVulkanCopy(false,0,100,65536));

int main() {
    std::mt19937_64 gen{0x20261110};
    for (unsigned i=0;i<1000000;++i) {
        const std::uint64_t size=1+(gen()%10000000);
        const std::uint64_t src=gen()%0x40000000ull;
        const std::uint64_t dst=gen()%0x40000000ull;
        const bool expected=src+size<=dst || dst+size<=src;
        assert(NonOverlappingVulkanCopy(true,src,dst,size)==expected);
        assert(NonOverlappingVulkanCopy(false,src,dst,size));
        const bool candidate=EligibleLargeScalarCopy(
            SHADER,SHADER,size/4,size/4,src,dst,size);
        assert(candidate==(expected && size>65536 && size<=UINT32_MAX));
    }
    puts("TLOU large scalar Vulkan transfer: 1M ranges, overlaps, boundaries and shader identity PASS");
}
