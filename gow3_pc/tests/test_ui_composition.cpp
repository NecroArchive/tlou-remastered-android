// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cstdio>
#include "gpu/shadps4/video_core/renderer_vulkan/ui_composition.h"

int main() {
    using namespace Vulkan::UiComposition;
    // Title/loading menus have no depth/camera. UI must still go to the native target.
    assert(Choose(true, true, false, true) == Background::Copy);
    assert(Choose(true, true, false, false) == Background::Copy);
    // In-game HUD: first reconstruct the scene, then draw the native UI over it.
    assert(Choose(true, true, true, true) == Background::Temporal);
    // Runtime FSR disable/failure must not change UI resolution.
    assert(Choose(true, true, true, false) == Background::Copy);
    assert(Choose(true, false, true, true) == Background::None);
    assert(Choose(false, true, true, true) == Background::None);
    assert(NativeViewport(1920, -1080));
    assert(!NativeViewport(960, -540));
    assert(MovieShader(0x34e8a281) && MovieShader(0x24042a9b));
    assert(!MovieShader(0x0b0acf50)); // fullscreen post/tonemap also has a native viewport
    assert(!MovieShader(0x6c62a79f)); // shadow geometry
    // Logs show 720p scene plus a separate 1080p reticle/UI pass. The
    // compositor must begin on this 1080p pass, not only on a 720p movie.
    assert(NativeUiTransition(true, true, false, 1920,1080, 1280,720));
    assert(NativeUiTransition(true, true, false, 1920,1080, 960,540));
    assert(NativeUiTransition(true, true, false, 1920,1080, 640,360));
    assert(!NativeUiTransition(false,true,false,1920,1080,1280,720));
    assert(!NativeUiTransition(true,false,false,1920,1080,1280,720));
    assert(!NativeUiTransition(true,true,true,1920,1080,1280,720));
    assert(!NativeUiTransition(true,true,false,1280,720,1280,720));
    assert(!NativeUiTransition(true,true,false,1920,1080,1920,1080));
    assert(Choose(true,true,false,false)==Background::Copy);
    const auto display = Scale(960, 540, 1920, 1080, false);
    assert(display[0] == 2 && display[1] == 2);
    const auto ui = Scale(960, 540, 1920, 1080, true);
    assert(ui[0] == 1 && ui[1] == 1); // don't double an already native UI viewport
    // TLOU direct-resolution 720p path: the last guest frame gets a
    // single 1.5x spatial stretch, while native-HUD/aim vertices are not
    // stretched again. No FSR provider is required to pick Copy.
    assert(Choose(true, true, false, false) == Background::Copy);
    const auto guest720 = Scale(1280, 720, 1920, 1080, false);
    assert(guest720[0] == 1.5f && guest720[1] == 1.5f);
    const auto hud720 = Scale(1280, 720, 1920, 1080, true);
    assert(hud720[0] == 1.f && hud720[1] == 1.f);
    const auto larger = Scale(960, 540, 3840, 2160, true);
    assert(larger[0] == 2 && larger[1] == 2);
    std::puts("UI composition: PASS (menu without camera, HUD, FSR off, viewport scaling)");
}
