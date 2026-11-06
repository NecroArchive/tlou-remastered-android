#include "gow3_movie.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

// A scratch allocation is intentionally unavailable: 720p video must fit in the decoder
// buffer instead of reserving more flexible guest memory.
extern "C" void* runtime_map_guest_memory(std::uint64_t) { return nullptr; }

int main() {
    assert(setenv("GOW3_MOVIE_SIZE", "1280x720", 1) == 0);
    std::uint32_t w = 0, h = 0;
    assert(Gow3Movie::DisplaySize(w, h) && w == 1280 && h == 720);
    assert(Gow3Movie::ScaledPitch() == 1280 && Gow3Movie::ScaledRows() == 768);
    assert(Gow3Movie::ScaledChromaPitch() == 1280);
    assert(Gow3Movie::ScaledFrameBytes() == 1474560);

    constexpr std::uint64_t decoder_bytes = 4194304;
    constexpr std::uint64_t native_picture_bytes = 1920ull * 1088 * 3 / 2;
    assert(Gow3Movie::CanWriteInPlace(1920, 1080, decoder_bytes - 256));
    assert(!Gow3Movie::CanWriteInPlace(1920, 1080, 1474559));
    assert(!Gow3Movie::CanWriteInPlace(640, 360, decoder_bytes - 256));
    assert(Gow3Movie::ScaledFrameBytes() < native_picture_bytes);
    assert(native_picture_bytes < decoder_bytes);

    std::vector<std::uint8_t> buffer(decoder_bytes, 0);
    const std::uint64_t base = reinterpret_cast<std::uint64_t>(buffer.data());
    const std::uint64_t movie_uv_offset = 1280ull * 768;

    // Original decoder output, before conversion.
    Gow3Movie::RecordFrame(base, 1920, 1080, 1920, 1088);
    std::uint64_t addr = base;
    std::uint32_t width = 1280, height = 720, pitch = 1280;
    assert(Gow3Movie::Remap(addr, width, height, pitch, 8));
    assert(addr == base && width == 1920 && height == 1080 && pitch == 1920);
    addr = base + movie_uv_offset;
    width = 640; height = 360; pitch = 640;
    assert(Gow3Movie::Remap(addr, width, height, pitch, 16));
    assert(addr == base + 1920ull * 1088 && width == 960 && height == 540 && pitch == 960);

    // After an in-place downscale, the exact texture descriptors the movie player requests
    // must map Y to base and UV to base + scaled pitch * aligned rows.
    Gow3Movie::RecordFrame(base, 1280, 720, 1280, 768);
    addr = base; width = 1280; height = 720; pitch = 1280;
    assert(Gow3Movie::Remap(addr, width, height, pitch, 8));
    assert(addr == base && width == 1280 && height == 720 && pitch == 1280);
    addr = base + movie_uv_offset; width = 640; height = 360; pitch = 640;
    assert(Gow3Movie::Remap(addr, width, height, pitch, 16));
    assert(addr == base + movie_uv_offset && width == 640 && height == 360 && pitch == 640);
    assert(movie_uv_offset + 1280ull * (768 / 2) <= native_picture_bytes);

    const std::uint32_t movie_shader[] = {0x3a088889, 0x3a70f0f1};
    assert(Gow3Movie::IsMovieShader(movie_shader, 2));
    float factor = 0;
    assert(Gow3Movie::MovieShaderConstant(0x3a088889, factor));
    assert(std::fabs(factor - 1.0f / 1280.0f) < 0.000001f);
    std::puts("Movie NV12: in-place capacity, Y/UV offsets and shader scale pass");
}
