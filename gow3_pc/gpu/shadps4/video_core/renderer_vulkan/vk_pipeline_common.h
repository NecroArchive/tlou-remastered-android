// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <span>

#include "shader_recompiler/profile.h"
#include "shader_recompiler/runtime_info.h"
#include "video_core/renderer_vulkan/vk_common.h"

#include <boost/container/small_vector.hpp>

namespace Shader {
struct Info;
struct PushData;
} // namespace Shader

namespace Vulkan {

static constexpr auto AllGraphicsStageBits =
    vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eTessellationControl |
    vk::ShaderStageFlagBits::eTessellationEvaluation | vk::ShaderStageFlagBits::eGeometry |
    vk::ShaderStageFlagBits::eFragment;

class Instance;
class Scheduler;
class DescriptorHeap;

class Pipeline {
public:
    Pipeline(const Instance& instance, Scheduler& scheduler, DescriptorHeap& desc_heap,
             const Shader::Profile& profile, vk::PipelineCache pipeline_cache,
             bool is_compute = false);
    virtual ~Pipeline();

    vk::Pipeline Handle() const noexcept {
        return *pipeline;
    }

    vk::PipelineLayout GetLayout() const noexcept {
        return *pipeline_layout;
    }

    auto GetStages() const {
        static_assert(static_cast<u32>(Shader::SwStage::Compute) == Shader::MaxStageTypes - 1);
        if (is_compute) {
            return std::span{stages.cend() - 1, stages.cend()};
        } else {
            return std::span{stages.cbegin(), stages.cend() - 1};
        }
    }

    const Shader::Info& GetStage(Shader::SwStage stage) const noexcept {
        return *stages[u32(stage)];
    }

    bool IsCompute() const {
        return is_compute;
    }

    // Immutable shader-layout predicate cached once per graphics pipeline.
    // The draw path still validates every dynamic image and every GPU hazard.
    bool CanBindTexturesOnHelper() const noexcept {
        return texture_helper_compatible;
    }

    /// gow3: scheduler tick of the last draw or dispatch that took it (the warm-up order).
    mutable u64 last_use = 0;

    using DescriptorWrites = std::vector<vk::WriteDescriptorSet>;
    // The spans own no data: caller holds both arrays until this call returns.
    // Deferred recording snapshots each contiguous array once, not once per descriptor.
    void BindResources(
        DescriptorWrites& set_writes, const Shader::PushData& push_data,
        std::span<const vk::DescriptorBufferInfo> source_buffers = {},
        std::span<const vk::DescriptorImageInfo> source_images = {}) const;

protected:
    [[nodiscard]] std::string GetDebugString() const;
    void RefreshTextureHelperEligibility();

    const Instance& instance;
    Scheduler& scheduler;
    DescriptorHeap& desc_heap;
    const Shader::Profile& profile;
    vk::UniquePipeline pipeline;
    vk::UniquePipelineLayout pipeline_layout;
    vk::UniqueDescriptorSetLayout desc_layout;
    std::array<const Shader::Info*, Shader::MaxStageTypes> stages{};
    bool uses_push_descriptors{};
    bool texture_helper_compatible = false;
    bool is_compute;
};

} // namespace Vulkan
