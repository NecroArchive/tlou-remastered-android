// SPDX-License-Identifier: GPL-2.0-or-later
// TLOU Android: FSR disabled, but native 1920x1080 logical aim and UI preserved.
// The renderer chooses these SceneTargets sizes and the existing RunUiOnly
// Vulkan linear blit instead of a temporal FSR dispatch.
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include "gpu/shadps4/video_core/renderer_vulkan/scene_resolution.h"
#include "gpu/shadps4/video_core/renderer_vulkan/ui_composition.h"

int main() {
    using Vulkan::SceneResolution::ForPreset;
    using Vulkan::SceneResolution::Size;
    using Vulkan::UiComposition::Background;
    using Vulkan::UiComposition::Choose;
    using Vulkan::UiComposition::Scale;
    assert((ForPreset(0) == Size{1920,1080}));
    const auto p720 = ForPreset(1);
    const auto p540 = ForPreset(3);
    const auto p360 = ForPreset(4);
    assert((p720 == Size{1280,720}));
    assert((p540 == Size{960,540}));
    assert((p360 == Size{640,360}));
    // When temporal FSR is off, the copy path still composites an output-size
    // background BEFORE the game's original 1080p UI (including its aim).
    for (const auto scene: {p720,p540,p360}) {
        assert(scene.width < 1920 && scene.height < 1080);
        assert(Choose(true, true, true, false) == Background::Copy);
        assert(Choose(true, true, false, false) == Background::Copy);
        assert(Choose(true, false, true, false) == Background::None);
        const auto ui = Scale(1920,1080,1920,1080,true);
        assert(ui[0] == 1.0f && ui[1] == 1.0f);
    }
    std::puts("TLOU spatial targets/UI without FSR: PASS");
}
