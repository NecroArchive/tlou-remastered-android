// Qualcomm proprietary Vulkan shader-compiler compatibility.
// Some valid compute SPIR-V 1.6 modules with explicit float-control modes
// fail vkCreateComputePipelines with VK_ERROR_UNKNOWN (TLOU log 32).
// This narrowly scoped alternate module leaves all instructions, descriptors,
// local workgroup size, and 1D storage-image operations intact. It drops only
// optional floating-point execution-mode promises, so denormal and signed-zero
// behavior may differ. Use only after the ORIGINAL pipeline has already failed.
#pragma once

#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace Vulkan::QualcommFloatCompat {

inline bool DropExplicitFloatControls(std::span<const uint32_t> original,
                                      std::vector<uint32_t>& alternative) {
    alternative.clear();
    constexpr uint32_t Magic = 0x07230203;
    constexpr uint32_t OpCapability = 17, OpExtension = 10, OpExecutionMode = 16;
    constexpr uint32_t CapabilityDenormFlushToZero = 4465;
    constexpr uint32_t CapabilitySignedZeroInfNanPreserve = 4466;
    constexpr uint32_t ExecutionDenormFlushToZero = 4460;
    constexpr uint32_t ExecutionSignedZeroInfNanPreserve = 4461;
    constexpr char FloatControls[] = "SPV_KHR_float_controls";
    if (original.size() < 5 || original.size() > 1024 * 1024 ||
        original[0] != Magic) return false;

    // Reject unsupported combinations rather than creating an invalid module
    // whose OpExtension and capabilities no longer agree.
    bool contains_mode = false, contains_capability = false;
    for (size_t pos = 5; pos < original.size();) {
        const uint32_t word = original[pos];
        const size_t count = word >> 16;
        const uint32_t op = word & 0xffff;
        if (!count || count > original.size() - pos) return false;
        if (op == OpCapability && count == 2) {
            const uint32_t cap = original[pos + 1];
            if (cap >= 4464 && cap <= 4468 &&
                cap != CapabilityDenormFlushToZero &&
                cap != CapabilitySignedZeroInfNanPreserve) return false;
            if (cap == CapabilityDenormFlushToZero ||
                cap == CapabilitySignedZeroInfNanPreserve) contains_capability = true;
        }
        if (op == OpExecutionMode && count >= 3) {
            const uint32_t mode = original[pos + 2];
            if (mode >= 4459 && mode <= 4463 &&
                mode != ExecutionDenormFlushToZero &&
                mode != ExecutionSignedZeroInfNanPreserve) return false;
            if (mode == ExecutionDenormFlushToZero ||
                mode == ExecutionSignedZeroInfNanPreserve) {
                if (count != 4 || original[pos + 3] != 32) return false;
                contains_mode = true;
            }
        }
        pos += count;
    }
    if (!contains_mode || !contains_capability) return false;
    alternative.insert(alternative.end(), original.begin(), original.begin() + 5);
    bool dropped_mode = false, dropped_capability = false;
    for (size_t pos = 5; pos < original.size();) {
        const size_t count = original[pos] >> 16;
        const uint32_t op = original[pos] & 0xffff;
        bool skip = false;
        if (op == OpCapability && count == 2 &&
            (original[pos + 1] == CapabilityDenormFlushToZero ||
             original[pos + 1] == CapabilitySignedZeroInfNanPreserve)) {
            skip = dropped_capability = true;
        } else if (op == OpExecutionMode && count == 4 &&
                   (original[pos + 2] == ExecutionDenormFlushToZero ||
                    original[pos + 2] == ExecutionSignedZeroInfNanPreserve)) {
            skip = dropped_mode = true;
        } else if (op == OpExtension && count > 1) {
            // The SPIR-V literal is padded with NUL bytes to the next word.
            const size_t byte_count = (count - 1) * sizeof(uint32_t);
            const char* bytes = reinterpret_cast<const char*>(original.data() + pos + 1);
            constexpr size_t prefix_bytes = sizeof(FloatControls);
            if (byte_count >= prefix_bytes &&
                std::memcmp(bytes, FloatControls, prefix_bytes) == 0) skip = true;
        }
        if (!skip) {
            alternative.insert(alternative.end(), original.begin() + pos,
                               original.begin() + pos + count);
        }
        pos += count;
    }
    if (!dropped_mode || !dropped_capability) {
        alternative.clear();
        return false;
    }
    return true;
}

} // namespace Vulkan::QualcommFloatCompat
