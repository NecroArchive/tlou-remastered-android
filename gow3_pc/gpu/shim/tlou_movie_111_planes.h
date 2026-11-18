// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>

namespace TlouMovie111 {
// The 01.11 720p/540p/360p framebuffer path can use 16-row-aligned UV
// offsets or retain native-sized movie plane descriptors. Only recorded
// movie buffers may be remapped; ordinary game textures are excluded.
constexpr std::uint32_t Align16(std::uint32_t v) { return (v + 15u) & ~15u; }
constexpr std::uint32_t Align64(std::uint32_t v) { return (v + 63u) & ~63u; }
constexpr std::uint64_t MovieUvOffset(std::uint32_t width,std::uint32_t height) {
    return std::uint64_t(Align64(width))*Align64(height);
}
constexpr std::uint64_t ShortUvOffset(std::uint32_t width,std::uint32_t height) {
    return std::uint64_t(Align64(width))*Align16(height);
}
constexpr bool LumaShape(std::uint32_t width,std::uint32_t height,
                         std::uint32_t targetWidth,std::uint32_t targetHeight) {
    return (width==targetWidth && (height==targetHeight || height==Align16(targetHeight) ||
                                   height==Align64(targetHeight))) ||
           (width==1920 && (height==1080 || height==1088));
}
constexpr bool ChromaShape(std::uint32_t width,std::uint32_t height,
                           std::uint32_t targetWidth,std::uint32_t targetHeight) {
    return (width==targetWidth/2 &&
            (height==targetHeight/2 || height==Align16(targetHeight)/2 ||
             height==Align64(targetHeight)/2)) ||
           (width==960 && (height==540 || height==544));
}
constexpr bool ChromaAddress(std::uint64_t address,std::uint64_t base,
                             std::uint32_t width,std::uint32_t height) {
    if(address<base)return false;
    const auto delta=address-base;
    return delta==MovieUvOffset(width,height) ||
           delta==ShortUvOffset(width,height) ||
           delta==MovieUvOffset(1920,1080);
}
}  // namespace TlouMovie111
