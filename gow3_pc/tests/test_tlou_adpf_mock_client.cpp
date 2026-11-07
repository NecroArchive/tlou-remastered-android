// SPDX-License-Identifier: GPL-2.0-or-later
// Compile with -D__ANDROID__ on host; inject fake libandroid.so via LD_LIBRARY_PATH.
#include "video_core/renderer_vulkan/tlou_adpf.h"
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <dlfcn.h>
#include <thread>
#include <cstdio>

int main() {
    setenv("TLOU_ADPF", "1", 1);
    setenv("GOW3_FPS_LIMIT", "60", 1);
    Vulkan::TlouAdpf::OnFrame(123, 0, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    Vulkan::TlouAdpf::OnFrame(123, 124, 125);
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    Vulkan::TlouAdpf::OnFrame(123, 124, 125);
    auto* lib = dlopen("libandroid.so", RTLD_NOW);
    assert(lib);
    auto* reports = reinterpret_cast<int(*)()>(dlsym(lib, "tlou_mock_reports"));
    auto* threads = reinterpret_cast<int(*)()>(dlsym(lib, "tlou_mock_threads"));
    assert(reports && reports() == 2);
    assert(threads && threads() == 3);
    std::puts("TLOU ADPF native service ABI, late thread join and frame reports: PASS");
}
