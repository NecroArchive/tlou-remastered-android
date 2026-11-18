// Android Qualcomm external-driver compatibility: keep user-selected Turnip
// failures explicit, but recover from an incompatible vendor Vulkan HAL by
// loading the Android system's Vulkan entry point. Run as separate processes
// because the runtime intentionally caches the selected ICD with call_once.
#include "../gpu/shim/vk_driver_loader.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const bool vendor = std::strcmp(argv[1], "vendor") == 0;
    const bool turnip = std::strcmp(argv[1], "turnip") == 0;
    const bool system = std::strcmp(argv[1], "system") == 0;
    if (!vendor && !turnip && !system) return 2;
    if (vendor) {
        setenv("GOW3_VULKAN_DRIVER", "/missing/qcom/vulkan.adr891.so", 1);
    } else if (turnip) {
        setenv("GOW3_VULKAN_DRIVER", "/missing/mesa/libvulkan_freedreno.so", 1);
    } else {
        unsetenv("GOW3_VULKAN_DRIVER");
    }
    auto proc = gow3_vk_get_instance_proc_addr();
    const bool expected = !turnip; // CI has the system Vulkan loader.
    if ((proc != nullptr) != expected) {
        std::fprintf(stderr, "Expected Vulkan entry %s, got %p for mode %s\n",
                     expected ? "present" : "absent",
                     reinterpret_cast<void*>(proc), argv[1]);
        return 1;
    }
    std::printf("TLOU Vulkan %s: %s\n", argv[1], expected ? "system loader" : "hard failure");
    return 0;
}
