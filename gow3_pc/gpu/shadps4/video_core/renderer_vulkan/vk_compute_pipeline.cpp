// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include <boost/container/small_vector.hpp>

#include "shader_recompiler/info.h"
#include "video_core/cache_storage.h"
#include "video_core/renderer_vulkan/vk_compute_pipeline.h"
#include "video_core/renderer_vulkan/vk_qualcomm_float_compat.h"
#include "video_core/renderer_vulkan/vk_qualcomm_image1d_compat.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"

namespace Vulkan {

namespace {
// Only entered when the proprietary Qualcomm compiler fails every non-shader-
// altering pipeline retry. Preserve the actual SPIR-V generated for this
// shader so a future log contains reproducible input, not just ErrorUnknown.
// Normal shaders and Turnip never enumerate or read the on-disk shader cache.
// Limit both candidate count and size; never dump the game's whole shader DB.
void CaptureQualcommComputeFailure(u64 shader_hash, u64 pipeline_key) {
    const char* user_dir = std::getenv("GOW3_GPU_USER_DIR");
    if (!user_dir || !*user_dir) return;
    auto& db = Storage::DataBase::Instance();
    if (!db.IsOpened()) return;
    const std::string prefix = fmt::format("{:#018x}_", shader_hash);
    std::vector<std::string> names;
    db.ForEachBlobName(Storage::BlobType::ShaderBinary, [&](const std::string& name) {
        if (name.starts_with(prefix) && names.size() < 8) names.push_back(name);
    });
    if (names.empty()) {
        std::fprintf(stderr,
                     "Qualcomm compute: shader=%016llx has no saved SPIR-V blob\n",
                     static_cast<unsigned long long>(shader_hash));
        return;
    }
    std::error_code ec;
    const auto output = std::filesystem::path(user_dir);
    std::filesystem::create_directories(output, ec);
    if (ec) return;
    std::ofstream manifest(output / "qualcomm-compute-failed.txt",
                           std::ios::out | std::ios::trunc);
    manifest << fmt::format("shader={:016x} pipeline_key={:016x} variants={}\n",
                            shader_hash, pipeline_key, names.size());
    u32 saved = 0;
    for (const auto& name : names) {
        std::vector<u32> spv;
        db.Load(Storage::BlobType::ShaderBinary, name, spv);
        if (spv.size() < 5 || spv[0] != 0x07230203 || spv.size() > 1024 * 1024) continue;
        const auto file = fmt::format("qualcomm-compute-failed-{}.spv", saved);
        std::ofstream binary(output / file, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!binary) continue;
        binary.write(reinterpret_cast<const char*>(spv.data()),
                     std::streamsize(spv.size() * sizeof(u32)));
        if (!binary) continue;
        manifest << name << " => " << file << " " << (spv.size() * sizeof(u32))
                 << " bytes\n";
        std::fprintf(stderr, "Qualcomm compute: saved %s from %s (%zu bytes)\n",
                     file.c_str(), name.c_str(), spv.size() * sizeof(u32));
        ++saved;
    }
}

// ZIP32, Adreno 840: shader 0x4a705408 has one available SPIR-V
// permutation and uses SPV_KHR_float_controls with explicit 32-bit FTZ
// and signed-zero mode. The original valid module fails all driver-side
// retries. Try an alternate module without only those FP control promises.
// Do not change descriptor types, 1D storage image, workgroup dimensions,
// shader instructions, or the global GPU shader cache. This can alter FP
// denormal/signed-zero behavior, so log exactly when the workaround is used.
bool TryQualcommFloatControlVariant(vk::Device device, const vk::ComputePipelineCreateInfo& original,
                                   u64 shader_hash, vk::UniquePipeline& pipeline) {
    auto& db = Storage::DataBase::Instance();
    if (!db.IsOpened()) return false;
    const auto prefix = fmt::format("{:#018x}_", shader_hash);
    std::vector<std::string> names;
    db.ForEachBlobName(Storage::BlobType::ShaderBinary, [&](const std::string& name) {
        if (name.starts_with(prefix) && names.size() < 2) names.push_back(name);
    });
    // Never attach a different specialization's SPIR-V to this pipeline.
    if (names.size() != 1) {
        std::fprintf(stderr,
                     "Qualcomm compute FP fallback: %zu shader variants found; "
                     "refusing ambiguous shader=%016llx\n",
                     names.size(), static_cast<unsigned long long>(shader_hash));
        return false;
    }
    std::vector<u32> spirv;
    db.Load(Storage::BlobType::ShaderBinary, names[0], spirv);
    std::vector<u32> alternative;
    if (!QualcommFloatCompat::DropExplicitFloatControls(spirv, alternative)) {
        std::fprintf(stderr, "Qualcomm compute FP fallback: no supported "
                             "float-controls-only transform shader=%016llx\n",
                     static_cast<unsigned long long>(shader_hash));
        return false;
    }
    const vk::ShaderModuleCreateInfo module_info{
        .codeSize = alternative.size() * sizeof(u32),
        .pCode = alternative.data(),
    };
    auto [module_result, alt_module] = device.createShaderModuleUnique(module_info);
    if (module_result != vk::Result::eSuccess) {
        std::fprintf(stderr, "Qualcomm compute FP fallback: alternate module "
                             "rejected shader=%016llx result=%s\n",
                     static_cast<unsigned long long>(shader_hash),
                     vk::to_string(module_result).c_str());
        return false;
    }
    auto ci = original;
    ci.stage.module = *alt_module;
    // Preserve the original required subgroup-size state. This retry changes
    // only explicit floating-point controls in SPIR-V; group size, descriptors
    // and Vulkan shader stage are byte-for-byte identical.
    auto [result, fallback] =
        device.createComputePipelineUnique(vk::PipelineCache{}, ci);
    if (result != vk::Result::eSuccess) {
        std::fprintf(stderr, "Qualcomm compute FP fallback: alternate SPIR-V "
                             "also rejected shader=%016llx result=%s\n",
                     static_cast<unsigned long long>(shader_hash),
                     vk::to_string(result).c_str());
        return false;
    }
    pipeline = std::move(fallback);
    std::fprintf(stderr,
                 "Qualcomm compute recovered: FP control modes disabled for "
                 "shader=%016llx (%zu -> %zu bytes); numerical FP behavior may differ\n",
                 static_cast<unsigned long long>(shader_hash),
                 spirv.size() * sizeof(u32), alternative.size() * sizeof(u32));
    return true;
}

// TLOU ZIP33: Adreno 840 rejects shader 0x4a705408 even after disabling
// explicit SPIR-V float modes. Its non-arrayed 1D storage image is written
// using a uint2 coordinate; Y is ignored by the Vulkan image semantics.
// Retry using the existing scalar X id, with an otherwise byte-identical
// shader. If that fails, try X scalarization + the previously tested float
// modes removal. Do not affect Mesa Turnip or any other unverified shader.
bool TryQualcommNarrowImage1DVariant(vk::Device device,
                                    const vk::ComputePipelineCreateInfo& original,
                                    u64 shader_hash, vk::UniquePipeline& pipeline) {
    constexpr u64 kVerifiedTLOUShaderHash = 0x4a705408ull;
    if (shader_hash != kVerifiedTLOUShaderHash) return false;
    auto& db = Storage::DataBase::Instance();
    if (!db.IsOpened()) return false;
    const auto prefix = fmt::format("{:#018x}_", shader_hash);
    std::vector<std::string> names;
    db.ForEachBlobName(Storage::BlobType::ShaderBinary, [&](const std::string& name) {
        if (name.starts_with(prefix) && names.size() < 2) names.push_back(name);
    });
    if (names.size() != 1) {
        std::fprintf(stderr,
                     "Qualcomm compute image compatibility: refusing %zu ambiguous shader variants\n",
                     names.size());
        return false;
    }
    std::vector<u32> spirv;
    db.Load(Storage::BlobType::ShaderBinary, names[0], spirv);
    if (spirv.size() < 5 || spirv[0] != 0x07230203) return false;

    // The attached 6020-byte shader declares read-without-format but NEVER
    // executes OpImageRead. The VkDevice previously enabled only the write
    // feature, making the unused read capability an avoidable requirement.
    // Try removing that UNUSED declaration first (zero functional changes).
    // If needed, also narrow this non-arrayed 1D image write's uint2
    // coordinate to its precomputed scalar X; Y was never consumed.
    std::vector<u32> no_read, x_only, x_no_read, x_float, x_no_read_float;
    const bool have_no_read =
        QualcommImage1DCompat::DropUnusedImageReadCapability(spirv, no_read);
    const bool have_x =
        QualcommImage1DCompat::NarrowNonArrayed1DWrite(spirv, x_only);
    const bool have_x_no_read = have_no_read &&
        QualcommImage1DCompat::NarrowNonArrayed1DWrite(no_read, x_no_read);
    const bool have_x_float = have_x &&
        QualcommFloatCompat::DropExplicitFloatControls(x_only, x_float);
    const bool have_x_no_read_float = have_x_no_read &&
        QualcommFloatCompat::DropExplicitFloatControls(x_no_read, x_no_read_float);
    struct Candidate {
        const char* description;
        const std::vector<u32>* words;
    };
    std::vector<Candidate> candidates;
    if (have_no_read) candidates.push_back({"unused image read capability removed", &no_read});
    if (have_x_no_read) candidates.push_back({"1D coordinate X-only + unused read capability removed", &x_no_read});
    if (have_x) candidates.push_back({"1D coordinate X-only", &x_only});
    if (have_x_no_read_float) candidates.push_back({"1D coordinate X-only + no read cap + FP mode relaxed", &x_no_read_float});
    if (have_x_float) candidates.push_back({"1D coordinate X-only + FP mode relaxed", &x_float});
    if (candidates.empty()) {
        std::fprintf(stderr,
                     "Qualcomm compute image compatibility: no valid transformations for %016llx\n",
                     static_cast<unsigned long long>(shader_hash));
        return false;
    }
    for (const Candidate& candidate : candidates) {
        const vk::ShaderModuleCreateInfo module_info{
            .codeSize = candidate.words->size() * sizeof(u32),
            .pCode = candidate.words->data(),
        };
        auto [module_result, alt_module] = device.createShaderModuleUnique(module_info);
        if (module_result != vk::Result::eSuccess) {
            std::fprintf(stderr,
                         "Qualcomm compute image compatibility: module rejected (%s): %s\n",
                         candidate.description, vk::to_string(module_result).c_str());
            continue;
        }
        auto ci = original;
        ci.stage.module = *alt_module;
        // Preserve Vulkan descriptor layout, required subgroup size and
        // compute workgroup dimensions in every candidate.
        auto [result, fallback] =
            device.createComputePipelineUnique(vk::PipelineCache{}, ci);
        if (result == vk::Result::eSuccess) {
            pipeline = std::move(fallback);
            std::fprintf(stderr,
                         "Qualcomm compute recovered: %s shader=%016llx (%zu -> %zu bytes)\n",
                         candidate.description, static_cast<unsigned long long>(shader_hash),
                         spirv.size() * sizeof(u32), candidate.words->size() * sizeof(u32));
            return true;
        }
        std::fprintf(stderr,
                     "Qualcomm compute image compatibility: rejected (%s) shader=%016llx: %s\n",
                     candidate.description, static_cast<unsigned long long>(shader_hash),
                     vk::to_string(result).c_str());
    }
    return false;
}
} // namespace

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
    // VK_VENDOR_ID_QUALCOMM is shared by Turnip and the proprietary ICD.
    // The retry is NOT intended for Mesa: use Vulkan's actual driver ID.
    const bool qualcomm = instance.GetDriverID() == vk::DriverId::eQualcommProprietary;
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
    if (pipeline_result == vk::Result::eErrorUnknown && retry_enabled) {
        const char* float_retry = std::getenv("GOW3_QUALCOMM_FLOAT_CONTROLS_RETRY");
        if (!(float_retry && float_retry[0] == '0') &&
            TryQualcommFloatControlVariant(device, compute_pipeline_ci,
                                          info_.pgm_hash, pipe)) {
            pipeline_result = vk::Result::eSuccess;
        }
    }
    if (pipeline_result == vk::Result::eErrorUnknown && retry_enabled) {
        const char* image_retry = std::getenv("GOW3_QUALCOMM_IMAGE1D_RETRY");
        if (!(image_retry && image_retry[0] == '0') &&
            TryQualcommNarrowImage1DVariant(device, compute_pipeline_ci,
                                            info_.pgm_hash, pipe)) {
            pipeline_result = vk::Result::eSuccess;
        }
    }
    if (pipeline_result != vk::Result::eSuccess) {
        if (qualcomm) CaptureQualcommComputeFailure(info_.pgm_hash, compute_key.value);
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
