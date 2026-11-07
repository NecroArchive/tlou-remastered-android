// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// C ABI between the Android TLOU probe's real libSceVideodec implementation
// and the Vulkan texture/shader movie remapper in libgow3gpu.so.
// Only enabled by GOW3_MOVIE_SIZE=WxH for reduced-resolution gameplay.
// The FFmpeg-owned source frame is never modified. When the scaled output fits in the
// decoder's existing guest frame buffer, its contents are replaced in place.
// Larger target frames retain the separate scratch-memory fallback.
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// A capacity-checked downscale uses the existing game-owned decoded frame buffer rather than
// reserving more guest flexible memory. No overflow, no growing source, no zero byte capacity.
static inline int gow3_movie_can_scale_in_place(uint32_t src_width, uint32_t src_height,
                                                uint32_t dst_width, uint32_t dst_height,
                                                uint64_t scaled_bytes, uint64_t guest_capacity) {
    return src_width >= dst_width && src_height >= dst_height &&
           scaled_bytes != 0 && scaled_bytes <= guest_capacity;
}

// Returns 1 iff a resized NV12 frame was generated for the supplied decoded 1080p
// source. Otherwise guest video continues using its native data.
// src_uv is the INTERLEAVED UV plane, not separate U and V planes.
int gow3_movie_scale_nv12(uint64_t guest_base, uint64_t guest_capacity,
                          const uint8_t* src_y, const uint8_t* src_uv,
                          uint32_t src_width, uint32_t src_height,
                          int src_y_stride, int src_uv_stride,
                          uint32_t* display_width, uint32_t* display_height);
#ifdef __cplusplus
}
#endif
