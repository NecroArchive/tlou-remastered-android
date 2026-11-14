// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdlib>
#include <span>
#include <vector>

#include "video_core/renderer_vulkan/vk_descriptor_record.h"
#include "video_core/renderer_vulkan/tlou_buffer_descriptor_template.h"

#include <boost/container/small_vector.hpp>
#include <boost/container/static_vector.hpp>

#include "shader_recompiler/resource.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_pipeline_cache.h"
#include "video_core/renderer_vulkan/vk_pipeline_common.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"

namespace Vulkan {

Pipeline::Pipeline(const Instance& instance_, Scheduler& scheduler_, DescriptorHeap& desc_heap_,
                   const Shader::Profile& profile_, vk::PipelineCache pipeline_cache,
                   bool is_compute_ /*= false*/)
    : instance{instance_}, scheduler{scheduler_}, desc_heap{desc_heap_}, profile{profile_},
      is_compute{is_compute_} {}

Pipeline::~Pipeline() = default;

void Pipeline::InitializeBufferDescriptorTemplate(u32 buffer_count) {
    if (!uses_push_descriptors || buffer_count < 4 ||
        !VULKAN_HPP_DEFAULT_DISPATCHER.vkCreateDescriptorUpdateTemplate ||
        !VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdPushDescriptorSetWithTemplateKHR) {
        return;
    }
    std::vector<vk::DescriptorUpdateTemplateEntry> entries;
    entries.reserve(buffer_count);
    for (u32 i = 0; i < buffer_count; ++i) {
        entries.push_back(vk::DescriptorUpdateTemplateEntry{
            .dstBinding = i,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eStorageBuffer,
            .offset = i * sizeof(vk::DescriptorBufferInfo),
            .stride = sizeof(vk::DescriptorBufferInfo),
        });
    }
    const vk::DescriptorUpdateTemplateCreateInfo info{
        .descriptorUpdateEntryCount = buffer_count,
        .pDescriptorUpdateEntries = entries.data(),
        .templateType = vk::DescriptorUpdateTemplateType::ePushDescriptorsKHR,
        .pipelineBindPoint = vk::PipelineBindPoint::eCompute,
        .pipelineLayout = *pipeline_layout,
        .set = 0,
    };
    auto [result, update] = instance.GetDevice().createDescriptorUpdateTemplateUnique(info);
    if (result == vk::Result::eSuccess) {
        buffer_descriptor_template = std::move(update);
        buffer_descriptor_count = buffer_count;
    }
}

// The stage layout never changes for a compiled graphics pipeline. Rechecking
// every image mip mode and every buffer write flag for each draw repeats work
// that was already known at pipeline construction/publication.
void Pipeline::RefreshTextureHelperEligibility() {
    texture_helper_compatible = false;
    if (is_compute) {
        return;
    }
    bool has_images = false;
    for (const Shader::Info* stage : GetStages()) {
        if (!stage) {
            continue;
        }
        has_images |= !stage->images.empty();
        for (const auto& image : stage->images) {
            // TexturesBindableOnHelper would reject a storage-image write
            // before doing any actual image binding; avoid the fork entirely.
            if (image.is_written ||
                image.mip_fallback_mode != Shader::MipStorageFallbackMode::None) {
                return;
            }
        }
        for (const auto& buffer : stage->buffers) {
            if (!buffer.IsSpecial() && buffer.is_written) {
                return;
            }
        }
    }
    texture_helper_compatible = has_images;
}

void Pipeline::BindResources(DescriptorWrites& set_writes, const Shader::PushData& push_data,
                             std::span<const vk::DescriptorBufferInfo> source_buffers,
                             std::span<const vk::DescriptorImageInfo> source_images) const {
    const auto bind_point =
        IsCompute() ? vk::PipelineBindPoint::eCompute : vk::PipelineBindPoint::eGraphics;
    const auto stage_flags = IsCompute() ? vk::ShaderStageFlagBits::eCompute : AllGraphicsStageBits;
    const vk::PipelineLayout layout = *pipeline_layout;
    scheduler.Record([layout, stage_flags, push_data](vk::CommandBuffer cmdbuf) {
        cmdbuf.pushConstants(layout, stage_flags, 0u, sizeof(push_data), &push_data);
    });

    // Bind descriptor set.
    if (set_writes.empty()) {
        return;
    }

    if (uses_push_descriptors) {
        if (buffer_descriptor_template && source_images.empty() &&
            Detail::MatchesBufferDescriptorTemplate(
                std::span<const vk::WriteDescriptorSet>{set_writes}, source_buffers,
                buffer_descriptor_count, vk::DescriptorType::eStorageBuffer)) {
            // The compact per-dispatch snapshot replaces the per-write copies
            // and pointer fixups, plus the driver's repeated write decoding.
            // Data and closure must stay in the same recording chunk.
            scheduler.ReserveRecordData(source_buffers.size_bytes() +
                                        alignof(vk::DescriptorBufferInfo) + 64);
            const auto buffers = scheduler.RecordData(source_buffers);
            const vk::DescriptorUpdateTemplate update = *buffer_descriptor_template;
            scheduler.Record([update, layout, buffers](vk::CommandBuffer cmdbuf) {
                Detail::PushBufferDescriptorTemplate(cmdbuf, update, layout, buffers);
            });
            return;
        }
        if (!scheduler.IsRecordingDeferred()) {
            scheduler.Record([&](vk::CommandBuffer cmdbuf) {
                cmdbuf.pushDescriptorSetKHR(bind_point, layout, 0, set_writes);
            });
            return;
        }
        // Optional TLOU optimization: keep the v0.21 per-write path by default.
        static const bool bulk_descriptors = [] {
            const char* value = std::getenv("TLOU_DESCRIPTOR_BULK");
            return value && value[0] == '1';
        }();
        if (!bulk_descriptors) {
            size_t bytes = set_writes.size() * sizeof(vk::WriteDescriptorSet) + 64;
            for (const auto& write : set_writes) {
                bytes += write.descriptorCount *
                             (sizeof(vk::DescriptorBufferInfo) + sizeof(vk::DescriptorImageInfo) +
                              sizeof(vk::BufferView)) + 32;
            }
            scheduler.ReserveRecordData(bytes);
            const auto writes =
                scheduler.RecordData(std::span<const vk::WriteDescriptorSet>{set_writes});
            auto* patched = const_cast<vk::WriteDescriptorSet*>(writes.data());
            for (size_t i = 0; i < writes.size(); ++i) {
                auto& write = patched[i];
                if (write.pBufferInfo) {
                    write.pBufferInfo = scheduler.RecordData(
                        std::span{write.pBufferInfo, write.descriptorCount}).data();
                }
                if (write.pImageInfo) {
                    write.pImageInfo = scheduler.RecordData(
                        std::span{write.pImageInfo, write.descriptorCount}).data();
                }
                if (write.pTexelBufferView) {
                    write.pTexelBufferView = scheduler.RecordData(
                        std::span{write.pTexelBufferView, write.descriptorCount}).data();
                }
            }
            scheduler.Record([bind_point, layout, writes](vk::CommandBuffer cmdbuf) {
                cmdbuf.pushDescriptorSetKHR(bind_point, layout, 0, writes);
            });
            return;
        }
        // The current draw owns two stable, contiguous arrays. Snapshot each one once
        // before deferring the Vulkan push-descriptor command; relocate pointers into
        // those copies. Descriptors not backed by these arrays retain the old per-write
        // snapshot path. No descriptor is reused across draws or guest/GPU writes.
        using Detail::DescriptorSourceOffset;
        using Detail::InvalidDescriptorOffset;
        const auto buffer_offset = [&](const vk::WriteDescriptorSet& w) {
            return DescriptorSourceOffset(source_buffers, w.pBufferInfo, w.descriptorCount);
        };
        const auto image_offset = [&](const vk::WriteDescriptorSet& w) {
            return DescriptorSourceOffset(source_images, w.pImageInfo, w.descriptorCount);
        };
        // Reserve enough for every copy plus alignment and the deferred command. This
        // keeps the entire snapshot in the same RecordChunk; otherwise a captured span
        // could outlive its chunk while the recording thread is using it.
        size_t bytes = set_writes.size() * sizeof(vk::WriteDescriptorSet) +
                       alignof(vk::WriteDescriptorSet) +
                       source_buffers.size_bytes() + alignof(vk::DescriptorBufferInfo) +
                       source_images.size_bytes() + alignof(vk::DescriptorImageInfo) + 64;
        for (const auto& w : set_writes) {
            if (w.pBufferInfo && buffer_offset(w) == InvalidDescriptorOffset) {
                bytes += w.descriptorCount * sizeof(vk::DescriptorBufferInfo) +
                         alignof(vk::DescriptorBufferInfo);
            }
            if (w.pImageInfo && image_offset(w) == InvalidDescriptorOffset) {
                bytes += w.descriptorCount * sizeof(vk::DescriptorImageInfo) +
                         alignof(vk::DescriptorImageInfo);
            }
            if (w.pTexelBufferView) {
                bytes += w.descriptorCount * sizeof(vk::BufferView) + alignof(vk::BufferView);
            }
        }
        scheduler.ReserveRecordData(bytes);
        const auto writes =
            scheduler.RecordData(std::span<const vk::WriteDescriptorSet>{set_writes});
        const auto saved_buffers = scheduler.RecordData(source_buffers);
        const auto saved_images = scheduler.RecordData(source_images);
        auto* patched = const_cast<vk::WriteDescriptorSet*>(writes.data());
        for (size_t i = 0; i < writes.size(); ++i) {
            auto& w = patched[i];
            if (w.pBufferInfo) {
                const size_t offset = buffer_offset(w);
                w.pBufferInfo = offset == InvalidDescriptorOffset
                                    ? scheduler.RecordData(std::span{w.pBufferInfo, w.descriptorCount}).data()
                                    : saved_buffers.data() + offset;
            }
            if (w.pImageInfo) {
                const size_t offset = image_offset(w);
                w.pImageInfo = offset == InvalidDescriptorOffset
                                   ? scheduler.RecordData(std::span{w.pImageInfo, w.descriptorCount}).data()
                                   : saved_images.data() + offset;
            }
            if (w.pTexelBufferView) {
                w.pTexelBufferView =
                    scheduler.RecordData(std::span{w.pTexelBufferView, w.descriptorCount}).data();
            }
        }
        scheduler.Record([bind_point, layout, writes](vk::CommandBuffer cmdbuf) {
            cmdbuf.pushDescriptorSetKHR(bind_point, layout, 0, writes);
        });
        return;
    }

    // Descriptor set updates are device calls: they stay on this thread.
    const auto desc_set = desc_heap.Commit(*desc_layout);
    for (auto& set_write : set_writes) {
        set_write.dstSet = desc_set;
    }
    instance.GetDevice().updateDescriptorSets(set_writes, {});
    scheduler.Record([bind_point, layout, desc_set](vk::CommandBuffer cmdbuf) {
        cmdbuf.bindDescriptorSets(bind_point, layout, 0, desc_set, {});
    });
}

std::string Pipeline::GetDebugString() const {
    std::string stage_desc;
    for (const auto& stage : stages) {
        if (stage) {
            const auto shader_name = PipelineCache::GetShaderName(stage->hw_stage, stage->pgm_hash);
            if (stage_desc.empty()) {
                stage_desc = shader_name;
            } else {
                stage_desc = fmt::format("{},{}", stage_desc, shader_name);
            }
        }
    }
    return stage_desc;
}

} // namespace Vulkan
