// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <span>

#include "shader_recompiler/frontend/fetch_shader.h"
#include "video_core/renderer_vulkan/vk_pipeline_cache.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"

namespace Vulkan {

namespace PipelineBinaryCache {
using Key = std::vector<u8>;
struct Binary {
    Key key;
    std::vector<u8> data;
    /// gow3: a large binary stored once beside the pipeline files (StoreShared), `data` empty.
    /// Decode leaves it null and sets shared_size/shared_hash; Load reads it.
    std::shared_ptr<const std::vector<u8>> shared;
    u32 shared_size = 0;
    u64 shared_hash = 0;

    std::span<const u8> Bytes() const {
        return shared ? std::span<const u8>{*shared} : std::span<const u8>{data};
    }
};
constexpr size_t MaxFileSize = 64 * 1024 * 1024;
constexpr u32 MaxBinaries = 32;
/// gow3 25 Oct 2026: binaries this large are stored once by key and content (NVIDIA's driver
/// returns the same 2 MiB binary for over half of The Last of Us's pipelines: 7.2 GB of copies,
/// re-read by every warm-up).
constexpr size_t SharedMinSize = 256 * 1024;

/// Pipeline files with shared binaries are version 2; the others stay version 1.
std::vector<u8> Encode(const Key& global, const Key& pipeline, u64 graphics_hash,
                       const std::vector<Binary>& binaries);
std::optional<std::vector<Binary>> Decode(std::span<const u8> bytes, const Key& global,
                                          const Key& pipeline, u64 graphics_hash);
bool Save(const std::filesystem::path& file, std::span<const u8> bytes);
std::optional<std::vector<Binary>> Load(const std::filesystem::path& file, const Key& global,
                                        const Key& pipeline, u64 graphics_hash);
/// gow3: moves the binaries of SharedMinSize or more out of `binaries` into the shared store of
/// `directory` (written once). False when one could not be stored (they stay inline).
bool StoreShared(const std::filesystem::path& directory, std::vector<Binary>& binaries);
/// gow3: whether `binaries` holds an inline binary StoreShared would move.
bool HasLargeInline(const std::vector<Binary>& binaries);
} // namespace PipelineBinaryCache

vk::ResultValue<vk::UniquePipeline> CreateCachedGraphicsPipeline(
    vk::Device device, vk::PipelineCache cache, const vk::GraphicsPipelineCreateInfo& info,
    const GraphicsPipelineKey& key, const vk::PipelineBinaryKeyKHR& global_key,
    const std::filesystem::path& directory);
void ReportPipelineBinaryCache();

void RegisterPipelineData(const ComputePipelineKey& key,
                          ComputePipeline::SerializationSupport& sdata);
void RegisterPipelineData(const GraphicsPipelineKey& key, u64 hash,
                          GraphicsPipeline::SerializationSupport& sdata);
void RegisterShaderMeta(const Shader::Info& info,
                        const std::optional<Shader::Gcn::FetchShaderData>& fetch_shader_data,
                        const Shader::StageSpecialization& spec, size_t perm_hash, size_t perm_idx);
void RegisterShaderBinary(std::vector<u32>&& spv, u64 pgm_hash, size_t perm_idx);
// Resolve and verify the exact permutation, never a different same-hash shader.
bool LoadExactComputeSpirv(u64 pgm_hash, u64 pipeline_key, std::vector<u32>& spv);

} // namespace Vulkan
