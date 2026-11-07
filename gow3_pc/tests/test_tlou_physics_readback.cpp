// SPDX-License-Identifier: GPL-2.0-or-later
#include "../gpu/shadps4/video_core/renderer_vulkan/tlou_physics_readback.h"
#include <cassert>
using TlouPhysicsReadback::Shape;

// CUSA00552 01.00: two separate device sessions logged exactly these outputs.
// The old shape matcher wrongly assumed the PC build's 48-byte V# stride.
static_assert(Shape(0x697cfcc4,6,4,1068,4272));
static_assert(Shape(0x697cfcc4,6,4,1620,6480));
static_assert(Shape(0x2198bf5b,5,4,1068,4272));
static_assert(Shape(0x2198bf5b,5,4,1620,6480));
static_assert(!Shape(0x2198bf5b,6,4,1068,4272));
static_assert(!Shape(0x697cfcc4,5,4,1620,6480));
static_assert(!Shape(0x106beafd,6,48,89,4272)); // PC handled by original hash path.
static_assert(!Shape(0xe30509e8,5,48,135,6480));
static_assert(!Shape(0x697cfcc5,6,4,1068,4272)); // Other shader may reuse this layout.
static_assert(!Shape(0x697cfcc4,5,4,1068,4272)); // Wrong slot.
static_assert(!Shape(0x697cfcc4,6,48,89,4272));  // Wrong descriptor interpretation.
static_assert(!Shape(0x697cfcc4,6,4,1068,4271)); // Truncated binding.
static_assert(!Shape(0x697cfcc4,6,4,1621,6484));// Unobserved buffer.
static_assert(!Shape(0x697cfcc4,6,52,89,4628));  // Skinning buffer.
static_assert(!Shape(0x697cfcc4,6,4,0,0));
int main() {
    constexpr unsigned long long hashes[]={0x697cfcc4,0x2198bf5b,0x697cfcc5,0x106beafd,0xe30509e8};
    for(auto hash:hashes)for(unsigned slot=0;slot<16;++slot)
        for(unsigned stride=1;stride<=64;++stride)
            for(unsigned n=1;n<=1700;++n){
                const bool expected=((hash==0x697cfcc4 && slot==6) ||
                                     (hash==0x2198bf5b && slot==5)) &&
                                    stride==4 && (n==1068 || n==1620);
                assert(Shape(hash,slot,stride,n,
                             static_cast<unsigned long long>(stride)*n)==expected);
            }
}
