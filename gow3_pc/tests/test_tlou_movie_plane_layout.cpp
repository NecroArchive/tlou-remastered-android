// SPDX-License-Identifier: GPL-2.0-or-later
// Static layout contract for native 1080p NV12 decode -> game-sized
// scratch movie planes. The game and its HUD remain unchanged.
#include <cassert>
#include <cstdint>
#include <initializer_list>
#include <type_traits>
#include "../gpu/shim/gow3_movie_bridge.h"

constexpr std::uint32_t Align64(std::uint32_t x) { return (x + 63u) & ~63u; }
struct Plane {
    std::uint32_t width, height, pitch, rows, chroma_pitch;
    std::uint64_t uv_offset, bytes;
};
constexpr Plane Layout(std::uint32_t w, std::uint32_t h) {
    const auto pitch = Align64(w);
    const auto rows = Align64(h);
    const auto chroma = Align64(w / 2) * 2;
    return {w,h,pitch,rows,chroma,std::uint64_t(pitch)*rows,
            std::uint64_t(pitch)*rows + std::uint64_t(chroma)*rows/2};
}
static_assert(std::is_same_v<decltype(&gow3_movie_scale_nv12),
    int (*)(std::uint64_t, std::uint64_t, const std::uint8_t*, const std::uint8_t*,
             std::uint32_t, std::uint32_t, int, int,
             std::uint32_t*, std::uint32_t*)>);
static_assert(Layout(1920,1080).uv_offset == 1920ull*1088ull);
static_assert(Layout(1280,720).uv_offset == 1280ull*768ull);
static_assert(Layout(1280,720).bytes == 1474560ull);
static_assert(Layout(960,540).chroma_pitch == 1024u);
static_assert(Layout(640,360).uv_offset == 640ull*384ull);
static_assert(Layout(1920,1080).uv_offset != Layout(1280,720).uv_offset);
// The precise green-video bug: the game asks for UV at the new 720p
// offset, while original decoded chroma starts after the 1080p luma.
int main() {
    // The real game decoder owns ~4 MiB per picture. At 720p the NV12 output fits
    // and must not allocate a scratch frame after 335 MiB guest flexible memory is full.
    assert(gow3_movie_can_scale_in_place(1920, 1080, 1280, 720,
                                         Layout(1280,720).bytes, 4194304ull));
    assert(!gow3_movie_can_scale_in_place(1920, 1080, 1280, 720,
                                          Layout(1280,720).bytes, 1474559ull));
    assert(!gow3_movie_can_scale_in_place(640, 360, 1280, 720,
                                          Layout(1280,720).bytes, 4194304ull));
    assert(gow3_movie_can_scale_in_place(1920, 1080, 960, 540,
                                         Layout(960,540).bytes, 4194304ull));
    // Interleaved UV stride is independently 64-texel-aligned (960 -> 1024).
    assert(Layout(960,540).pitch == 960 && Layout(960,540).chroma_pitch == 1024);
    for (auto p: {Layout(1280,720),Layout(960,540),Layout(640,360)}) {
        assert(p.width % 2 == 0 && p.height % 2 == 0);
        assert(p.uv_offset == std::uint64_t(p.pitch)*Align64(p.height));
        assert(p.bytes > p.uv_offset);
        assert(p.pitch >= p.width && p.chroma_pitch >= p.width);
        assert(p.rows >= p.height && p.rows%64==0);
    }
}
