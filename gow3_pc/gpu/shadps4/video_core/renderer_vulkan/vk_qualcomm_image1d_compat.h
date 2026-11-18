// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <span>
#include <vector>

namespace Vulkan::QualcommImage1DCompat {

// Work around a proprietary shader compiler rejecting an OpImageWrite for
// a non-arrayed 1D storage image when given an extra Y coordinate.
// SPIR-V allows unused coordinate components, but Vulkan's image mapping
// consumes only X for a non-arrayed 1D view. Narrow the coordinate to the
// ALREADY computed scalar X id. All other words (including Image1D type,
// storage-image descriptor, texel data, workgroup, and FP modes) stay equal.
// Reject anything not matching this fully checked one-write pattern.
inline bool NarrowNonArrayed1DWrite(std::span<const uint32_t> original,
                                   std::vector<uint32_t>& patched) {
    patched.clear();
    constexpr uint32_t kSpirvMagic = 0x07230203;
    if (original.size() < 5 || original.size() > 1024 * 1024 ||
        original[0] != kSpirvMagic) return false;

    constexpr uint32_t OpTypeInt = 21, OpTypeVector = 23, OpTypeImage = 25;
    constexpr uint32_t OpLoad = 61, OpCompositeConstruct = 80, OpImageWrite = 99;
    uint32_t uint32_type = 0, uint32_vec2 = 0, image1d_type = 0;
    uint32_t image_loaded_id = 0, coord_vec_id = 0, coord_x_id = 0;
    size_t write_coord_pos = 0;
    unsigned image_types = 0, image_loads = 0, coord_vectors = 0, writes = 0;
    for (size_t i = 5; i < original.size();) {
        const uint32_t word = original[i];
        const size_t n = word >> 16;
        const uint32_t op = word & 0xffffu;
        if (!n || n > original.size() - i) return false;
        if (op == OpTypeInt && n == 4 &&
            original[i + 2] == 32 && original[i + 3] <= 1) {
            // The shader has only one 32-bit unsigned type.
            if (original[i + 3] == 0) uint32_type = original[i + 1];
        } else if (op == OpTypeVector && n == 4 &&
                   original[i + 2] == uint32_type && original[i + 3] == 2) {
            uint32_vec2 = original[i + 1];
        } else if (op == OpTypeImage && n >= 9 &&
                   original[i + 3] == 1 &&  // Dim1D
                   original[i + 5] == 0 &&  // non-arrayed
                   original[i + 7] == 2) { // storage image
            image1d_type = original[i + 1];
            ++image_types;
        } else if (op == OpLoad && n >= 4 &&
                   original[i + 1] == image1d_type && image1d_type != 0) {
            image_loaded_id = original[i + 2];
            ++image_loads;
        } else if (op == OpCompositeConstruct && n == 5 &&
                   original[i + 1] == uint32_vec2 && uint32_vec2 != 0) {
            coord_vec_id = original[i + 2];
            coord_x_id = original[i + 3];
            ++coord_vectors;
        } else if (op == OpImageWrite) {
            ++writes;
            if (n != 5 || original[i + 1] != image_loaded_id ||
                original[i + 2] != coord_vec_id || original[i + 4] != 0 ||
                coord_x_id == 0) return false;
            write_coord_pos = i + 2;
        }
        i += n;
    }
    if (image_types != 1 || image_loads != 1 || coord_vectors != 1 ||
        writes != 1 || write_coord_pos == 0) return false;
    patched.assign(original.begin(), original.end());
    patched[write_coord_pos] = coord_x_id;
    return true;
}

// The 6020-byte ZIP33 shader declares StorageImageReadWithoutFormat
// (capability 55) but only writes its single storage image. Some Qualcomm
// drivers reject declared features not enabled on the VkDevice, even if no
// instruction uses them. Strip precisely this unused declaration; retain
// StorageImageWriteWithoutFormat (56), its OpImageWrite and all instructions.
inline bool DropUnusedImageReadCapability(std::span<const uint32_t> original,
                                          std::vector<uint32_t>& patched) {
    patched.clear();
    if (original.size() < 5 || original.size() > 1024 * 1024 ||
        original[0] != 0x07230203) return false;
    constexpr uint32_t OpCapability = 17, OpImageRead = 98,
                       OpImageSparseRead = 320, OpImageWrite = 99;
    constexpr uint32_t StorageImageReadWithoutFormat = 55;
    constexpr uint32_t StorageImageWriteWithoutFormat = 56;
    unsigned unused_read_cap = 0, write_cap = 0, writes = 0;
    for (size_t i = 5; i < original.size();) {
        const size_t n = original[i] >> 16;
        const uint32_t op = original[i] & 0xffffu;
        if (!n || n > original.size() - i) return false;
        if (op == OpImageRead || op == OpImageSparseRead) return false;
        if (op == OpCapability && n == 2) {
            unused_read_cap += original[i + 1] == StorageImageReadWithoutFormat;
            write_cap += original[i + 1] == StorageImageWriteWithoutFormat;
        }
        writes += op == OpImageWrite;
        i += n;
    }
    if (unused_read_cap != 1 || write_cap != 1 || writes != 1) return false;
    patched.insert(patched.end(), original.begin(), original.begin() + 5);
    for (size_t i = 5; i < original.size();) {
        const size_t n = original[i] >> 16;
        const uint32_t op = original[i] & 0xffffu;
        if (!(op == OpCapability && n == 2 &&
              original[i + 1] == StorageImageReadWithoutFormat)) {
            patched.insert(patched.end(), original.begin() + i, original.begin() + i + n);
        }
        i += n;
    }
    return patched.size() + 2 == original.size();
}

} // namespace Vulkan::QualcommImage1DCompat
