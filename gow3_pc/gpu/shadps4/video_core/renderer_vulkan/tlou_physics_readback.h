// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>

// The CUSA00552 01.00 Android runtime logs (two independent sessions on
// 2026-11-05) identify the GPU-written compute output shader 697cfcc4 at
// slot 6 with raw DWORD V# descriptors (stride 4), 1068/1620 records,
// and exactly 4272/6480 bytes. The earlier PC port's physics results were
// described as logical 48-byte records under different shader hashes.
//
// Only the *observed program hash AND exact binding layout* may enter the
// Android fallback. Other GPU writers can share the same byte size, so a
// generic size/stride heuristic is not safe. The legacy PC shader hashes
// are recognized separately by TlouEopReadbackBinding in vk_rasterizer.cpp.
//
// Caller must additionally establish: compute stage, GPU-written binding,
// EOP mode enabled, and readback quota available. Never widen this to
// Readback 2 or every written storage buffer.
namespace TlouPhysicsReadback {
constexpr bool Shape(std::uint64_t shader_hash, std::uint32_t slot,
                     std::uint32_t stride, std::uint64_t records,
                     std::uint64_t bytes) noexcept {
    // Logs from Adreno 830 (2026-11-06) additionally show the same two
    // physics-size outputs from shader 2198bf5b, slot 5. The older selective
    // readback covered only the matching sizes from 697cfcc4, slot 6.
    // Keep shader, slot, raw DWORD stride AND complete length as guards.
    if (stride != 4 || bytes != records * 4) return false;
    const bool observed_program =
        (shader_hash == 0x697cfcc4ull && slot == 6) ||
        (shader_hash == 0x2198bf5bull && slot == 5);
    return observed_program &&
           ((records == 1068 && bytes == 4272) ||
            (records == 1620 && bytes == 6480));
}
} // namespace TlouPhysicsReadback
