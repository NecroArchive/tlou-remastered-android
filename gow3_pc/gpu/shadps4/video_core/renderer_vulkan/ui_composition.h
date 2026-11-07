// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <array>
#include <cmath>
#include <cstdint>

namespace Vulkan::UiComposition {

enum class Background { None, Copy, Temporal };

// UI composition must not require scene depth/camera or an active FSR context.
constexpr Background Choose(bool scaled, bool ui_draw, bool scene_ready, bool fsr_active) {
    if (!scaled || !ui_draw) {
        return Background::None;
    }
    return scene_ready && fsr_active ? Background::Temporal : Background::Copy;
}

// In TLOU CUSA00552 01.00, direct 720p/540p/360p guest patches
// produce both a reduced-size 3D frame and a separate 1920x1080 UI
// target (720p/1080p target audits in the 2026-11-06 device logs).
// A compositor looking only for a reduced-size UI target never runs.
// Recognize a native UI target only after a different completed LDR
// scene target has been observed. Never intercept original 1080p games
// or the scene itself.
constexpr bool NativeUiTransition(bool direct_guest_scaling,
                                  bool ldr_ready, bool ldr_is_target,
                                  uint32_t target_width, uint32_t target_height,
                                  uint32_t guest_width, uint32_t guest_height) {
    return direct_guest_scaling && ldr_ready && !ldr_is_target &&
           target_width == 1920 && target_height == 1080 &&
           guest_width > 0 && guest_height > 0 &&
           guest_width < target_width && guest_height < target_height;
}

inline bool NativeViewport(float width, float height) {
    return std::abs(std::abs(width) - 1920.0f) < 0.5f &&
           std::abs(std::abs(height) - 1080.0f) < 0.5f;
}

// Scaleform draws, including the first stencil/movie pass. A native-size viewport
// alone also matches every fullscreen post pass when guest targets stay at 1080p.
constexpr bool MovieShader(uint64_t hash) {
    return hash == 0x34e8a281 || hash == 0x81d336ce || hash == 0x09957251 ||
           hash == 0x24042a9b || hash == 0xa400228b;
}

inline std::array<float, 2> Scale(uint32_t guest_width, uint32_t guest_height,
                                uint32_t output_width, uint32_t output_height,
                                bool native_coordinates) {
    return {float(output_width) / float(native_coordinates ? 1920 : guest_width),
            float(output_height) / float(native_coordinates ? 1080 : guest_height)};
}

} // namespace Vulkan::UiComposition
