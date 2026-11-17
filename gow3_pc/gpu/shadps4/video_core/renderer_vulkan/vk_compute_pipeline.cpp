// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdio>
#include <cstdlib>

#include <boost/container/small_vector.hpp>

#include "shader_recompiler/info.h"
#include "video_core/renderer_vulkan/vk_compute_pipeline.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"

namespace Vulkan {

ComputePipeline::ComputePipeline(const Instance& instance, Scheduler& scheduler,
                                 DescriptorHeap& desc_heap, const Shader::Profile& profile,
                                 vk::PipelineCache pipeline_cache, ComputePipelineKey compute_key_,
                                 const Shader::Info& info_, vk::ShaderModule module,
                                 SerializationSupport& sdata, bool preloading /*=false*/)
    : Pipeline{instance, scheduler, desc_heap, profile, pipeline_cache, true},
      compute_key{compute_key_} {
    auto& info = stages[int(Shader::SwStage::Compute)];
    info = &info_;
    const auto debug_str = GetDebugString();

    const vk::PipelineShaderStageRequiredSubgroupSizeCreateInfo subgroup_size_ci = {
        .requiredSubgroupSize = 64,
    };
    const vk::PipelineShaderStageCreateInfo shader_ci = {
        .pNext = instance.IsSubgroupSize64Supported() ? &subgroup_size_ci : nullptr,
        .stage = vk::ShaderStageFlagBits::eCompute,
        .module = module,
        .pName = "main",
    };

    u32 binding{};
    boost::container::small_vector<vk::DescriptorSetLayoutBinding, 32> bindings;
    for (const auto& buffer : info->buffers) {
        // During deserialization, we don't have access to the UD to fetch sharp data. To address
        // this properly we need to track shaprs or portion of them in `sdata`, but since we're
        // interested only in "is storage" flag (which is not even effective atm), we can take a
        // shortcut there.
        const auto sharp = preloading ? AmdGpu::Buffer{} : buffer.GetSharp(*info);
        bindings.push_back({
            .binding = binding++,
            .descriptorType = vk::DescriptorType::eStorageBuffer,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eCompute,
        });
    }
    for (const auto& image : info->images) {
        const u32 num_bindings = image.NumBindings(*info);
        bindings.push_back({
            .binding = binding,
            .descriptorType = image.is_written ? vk::DescriptorType::eStorageImage
                                               : vk::DescriptorType::eSampledImage,
            .descriptorCount = num_bindings,
            .stageFlags = vk::ShaderStageFlagBits::eCompute,
        });
        binding += num_bindings;
    }
    for (const auto& sampler : info->samplers) {
        bindings.push_back({
            .binding = binding++,
            .descriptorType = vk::DescriptorType::eSampler,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eCompute,
        });
    }

    const vk::PushConstantRange push_constants = {
        .stageFlags = vk::ShaderStageFlagBits::eCompute,
        .offset = 0,
        .size = sizeof(Shader::PushData),
    };

    uses_push_descriptors = binding < instance.MaxPushDescriptors();
    const auto flags = uses_push_descriptors
                           ? vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR
                           : vk::DescriptorSetLayoutCreateFlagBits{};
    const vk::DescriptorSetLayoutCreateInfo desc_layout_ci = {
        .flags = flags,
        .bindingCount = static_cast<u32>(bindings.size()),
        .pBindings = bindings.data(),
    };
    const auto device = instance.GetDevice();
    auto [descriptor_set_result, descriptor_set] =
        device.createDescriptorSetLayoutUnique(desc_layout_ci);
    ASSERT_MSG(descriptor_set_result == vk::Result::eSuccess,
               "Failed to create compute descriptor set layout: {}",
               vk::to_string(descriptor_set_result));
    desc_layout = std::move(descriptor_set);

    const vk::DescriptorSetLayout set_layout = *desc_layout;
    const vk::PipelineLayoutCreateInfo layout_info = {
        .setLayoutCount = 1U,
        .pSetLayouts = &set_layout,
        .pushConstantRangeCount = 1U,
        .pPushConstantRanges = &push_constants,
    };
    auto [layout_result, layout] = instance.GetDevice().createPipelineLayoutUnique(layout_info);
    ASSERT_MSG(layout_result == vk::Result::eSuccess,
               "Failed to create compute pipeline layout: {}", vk::to_string(layout_result));
    pipeline_layout = std::move(layout);
    SetObjectName(device, *pipeline_layout, "Compute PipelineLayout {}", debug_str);
    if (info->images.empty() && info->samplers.empty()) {
        InitializeBufferDescriptorTemplate(static_cast<u32>(info->buffers.size()));
    }

    const vk::ComputePipelineCreateInfo compute_pipeline_ci = {
        .stage = shader_ci,
        .layout = *pipeline_layout,
    };
    auto [pipeline_result, pipe] =
        instance.GetDevice().createComputePipelineUnique(pipeline_cache, compute_pipeline_ci);

    // Qualcomm's proprietary compiler sometimes returns VK_ERROR_UNKNOWN
    // for valid compute shaders (TLOU 01.11 Adreno 840, log 31). This is NOT
    // the earlier dlopen error of an external Qualcomm driver. The generic
    // assertion previously hid the shader hash and supplied no alternatives.
    // Do not retry for Turnip, which has a different shader compiler, or for
    // OOM/unsupported-feature results. Never drop compute output.
    const bool qualcomm = instance.GetVendorID() == 0x5143;
    const char* retry_env = std::getenv("GOW3_QUALCOMM_COMPUTE_RETRY");
    const bool retry_enabled = qualcomm && !(retry_env && retry_env[0] == '0');
    if (pipeline_result == vk::Result::eErrorUnknown && retry_enabled) {
        std::fprintf(stderr,
                     "Qualcomm compute compile: first attempt failed for "
                     "shader=%016llx key=%016llx (%s); buffers=%zu images=%zu "
                     "samplers=%zu subgroup64=%d native_subgroup=%u. "
                     "Trying isolated pipeline compilation.\n",
                     static_cast<unsigned long long>(info_.pgm_hash),
                     static_cast<unsigned long long>(compute_key.value),
                     vk::to_string(pipeline_result).c_str(), info->buffers.size(),
                     info->images.size(), info->samplers.size(),
                     shader_ci.pNext != nullptr, instance.SubgroupSize());
        std::fflush(stderr);
        // A cache can carry a driver compiler state incompatible with an
        // otherwise valid SPIR-V module. Empty cache changes no shader code.
        auto [uncached_result, uncached_pipe] =
            device.createComputePipelineUnique(vk::PipelineCache{}, compute_pipeline_ci);
        if (uncached_result == vk::Result::eSuccess) {
            pipe = std::move(uncached_pipe);
            pipeline_result = uncached_result;
            std::fprintf(stderr, "Qualcomm compute recovered: uncached shader=%016llx\n",
                         static_cast<unsigned long long>(info_.pgm_hash));
        } else {
            vk::ComputePipelineCreateInfo compatible = compute_pipeline_ci;
            // With native subgroup=64, omitting a redundant required-64 pNext
            // keeps wave width unchanged. Do not do this on native 32 devices.
            if (shader_ci.pNext != nullptr && instance.SubgroupSize() == 64) {
                compatible.stage.pNext = nullptr;
                auto [subgroup_result, subgroup_pipe] =
                    device.createComputePipelineUnique(vk::PipelineCache{}, compatible);
                if (subgroup_result == vk::Result::eSuccess) {
                    pipe = std::move(subgroup_pipe);
                    pipeline_result = subgroup_result;
                    std::fprintf(stderr,
                                 "Qualcomm compute recovered: native subgroup64 "
                                 "shader=%016llx\n",
                                 static_cast<unsigned long long>(info_.pgm_hash));
                } else {
                    pipeline_result = subgroup_result;
                }
            } else {
                pipeline_result = uncached_result;
            }
            if (pipeline_result != vk::Result::eSuccess) {
                // Last resort for known Qualcomm internal compiler errors:
                // preserve SPIR-V and resources but request a less aggressive
                // driver optimization path. Only the failing shader pays cost.
                compatible.flags |= vk::PipelineCreateFlagBits::eDisableOptimization;
                auto [compat_result, compat_pipe] =
                    device.createComputePipelineUnique(vk::PipelineCache{}, compatible);
                pipeline_result = compat_result;
                if (compat_result == vk::Result::eSuccess) {
                    pipe = std::move(compat_pipe);
                    std::fprintf(stderr,
                                 "Qualcomm compute recovered: disable-optimization "
                                 "shader=%016llx\n",
                                 static_cast<unsigned long long>(info_.pgm_hash));
                }
            }
        }
        std::fflush(stderr);
    }
    if (pipeline_result != vk::Result::eSuccess) {
        std::fprintf(stderr, "GPU compute pipeline unrecoverable: shader=%016llx "
                             "key=%016llx vendor=%#x error=%s\n",
                     static_cast<unsigned long long>(info_.pgm_hash),
                     static_cast<unsigned long long>(compute_key.value),
                     instance.GetVendorID(), vk::to_string(pipeline_result).c_str());
        std::fflush(stderr);
    }
    ASSERT_MSG(pipeline_result == vk::Result::eSuccess,
               "Failed to create compute pipeline shader {:#x}: {}",
               info_.pgm_hash, vk::to_string(pipeline_result));
    pipeline = std::move(pipe);
    SetObjectName(device, *pipeline, "Compute Pipeline {}", debug_str);
}

ComputePipeline::~ComputePipeline() = default;

} // namespace Vulkan
