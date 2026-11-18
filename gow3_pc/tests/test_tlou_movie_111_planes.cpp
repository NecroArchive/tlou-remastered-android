// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cstdint>
#include <initializer_list>
#include "../gpu/shim/tlou_movie_111_planes.h"

using namespace TlouMovie111;
static_assert(MovieUvOffset(1280,720)==983040);
static_assert(ShortUvOffset(1280,720)==921600);
static_assert(MovieUvOffset(1920,1080)==2088960);
static_assert(MovieUvOffset(960,540)==552960);
static_assert(ShortUvOffset(960,540)==522240);
int main() {
    const std::uint64_t base=0x180000000ull;
    for(auto size : {720u,540u,360u}) {
        const std::uint32_t width=size==720?1280:size==540?960:640;
        assert(ChromaAddress(base+MovieUvOffset(width,size),base,width,size));
        assert(ChromaAddress(base+ShortUvOffset(width,size),base,width,size));
        assert(ChromaAddress(base+MovieUvOffset(1920,1080),base,width,size));
        assert(!ChromaAddress(base+9999,base,width,size));
        assert(!ChromaAddress(base-1,base,width,size));
        assert(LumaShape(width,size,width,size));
        assert(LumaShape(width,Align64(size),width,size));
        assert(ChromaShape(width/2,Align64(size)/2,width,size));
        assert(LumaShape(1920,1080,width,size));
        assert(ChromaShape(960,540,width,size));
        assert(!LumaShape(width/2,size,width,size));
        assert(!ChromaShape(width*2,size+42,width,size));
    }
    assert(MovieUvOffset(1280,720)-ShortUvOffset(1280,720)==61440);
}
