// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <atomic>
#include <chrono>
#include <utility>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <cstdlib>
#include <initializer_list>
#include <vector>
#include <boost/container/small_vector.hpp>

#include "common/assert.h"
#include "shader_recompiler/backend/spirv/emit_spirv_discard_frag.h"
#include "shader_recompiler/backend/spirv/emit_spirv_quad_rect.h"
#include "video_core/renderer_vulkan/liverpool_to_vk.h"
#include "video_core/renderer_vulkan/vk_blend_rewrite.h"
#include "video_core/renderer_vulkan/vk_graphics_pipeline.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_pipeline_serialization.h"
#include "common/elf_info.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/renderer_vulkan/tlou_qualcomm_dead_int_decls.h"

namespace Vulkan {

using Shader::Backend::SPIRV::AuxShaderType;

// gow3: where pipeline construction time goes (reported when the background warm-up ends).
std::atomic<u64> g_gow3_pipeline_total_ns, g_gow3_pipeline_driver_ns;
std::atomic<u32> g_gow3_pipelines_built;
namespace {
using Clock = std::chrono::steady_clock;

// Exact captured TLOU graphics pair (guest VS c8987d69, PS b7ecfe4a).
// Both captures from two rejected pipeline keys were byte-identical.
// No shader hash alone is sufficient to authorize rewriting a permutation.
// Preserve all outputs, descriptors, execution modes and render state.
bool LowerCapturedQualcommGraphics(std::span<const u32> src, std::vector<u32>& dst,
                                   bool vertex) {
    dst.clear();
    if (src.size()<5 || src[0]!=0x07230203) return false;
    // New permutations of the same emitter layout are admitted only when
    // every removed declaration is proved unused. Keep exact old ALU lowering.
    if (QualcommDeadIntDecls::Strip(src,dst)) return true;
    u64 hash = 14695981039346656037ull;
    for (u32 word : src)
        for (unsigned shift = 0; shift < 32; shift += 8)
            hash = (hash ^ ((word >> shift) & 255u)) * 1099511628211ull;
    const bool wide_vertex=vertex && src.size()==9320 && src[3]==1549 &&
        hash==0xc40a1aaec9bc5787ull;
    const bool old_fragment=!vertex && src.size()==1710 && src[3]==244 &&
        hash==0x0515614fcf5f28e0ull;
    // Captured on 26 Nov 14:56. The three exact modules have no wide/narrow
    // arithmetic or memory accesses; only dead type/constant declarations.
    const bool simple_vertex=vertex && src.size()==1271 && src[3]==160 &&
        (hash==0x427ddcccf807bc1full || hash==0xeafbbb52a907f148ull ||
         hash==0x685e1111fc2df30aull);
    const bool simple_fragment=!vertex &&
        ((src.size()==794 && src[3]==86 && hash==0x6b90c09f2970206eull) ||
         (src.size()==987 && src[3]==116 && hash==0x7cfa7c6780a57826ull) ||
         // 26 Nov 15:56: exact captured variants. Removal of the same
         // 20 dead declarations leaves every executable instruction intact.
         (src.size()==1196 && src[3]==150 && hash==0x5e58a2cd42355ca8ull) ||
         (src.size()==1377 && src[3]==183 && hash==0x3febbe927ccab4d0ull) ||
         (src.size()==920 && src[3]==109 && hash==0x00cfcf50e42565b9ull));
    if(!(wide_vertex||old_fragment||simple_vertex||simple_fragment))return false;
    std::vector<u32> out(src.begin(), src.begin() + 5);
    u32 next = src[3];
    const u32 mask = next++, shift = next++;
    auto emit = [&](u32 op, std::initializer_list<u32> a) {
        out.push_back((u32(a.size()+1)<<16)|op);
        out.insert(out.end(), a.begin(), a.end());
    };
    auto bin = [&](u32 op, u32 a, u32 b) {
        const u32 id = next++;
        emit(op, {10,id,a,b}); return id;
    };
    // Unsigned full 64-bit product represented as low/high uint32 words.
    auto product = [&](u32 a,u32 b,u32 type,u32 result) {
        const u32 a0=bin(199,a,mask),a1=bin(194,a,shift);
        const u32 b0=bin(199,b,mask),b1=bin(194,b,shift);
        const u32 p0=bin(132,a0,b0),p1=bin(132,a0,b1);
        const u32 p2=bin(132,a1,b0),p3=bin(132,a1,b1);
        const u32 c0=bin(194,p0,shift),c1=bin(199,p1,mask),c2=bin(199,p2,mask);
        const u32 t=bin(128,bin(128,c0,c1),c2);
        const u32 h=bin(128,bin(128,bin(128,p3,bin(194,p1,shift)),
                                     bin(194,p2,shift)),bin(194,t,shift));
        const u32 low=bin(132,a,b);
        emit(80,{type,result,low,h});
    };
    unsigned extended=0,wide=0,converted=0;
    for (size_t i=5;i<src.size();) {
        const u32 n=src[i]>>16, op=src[i]&65535u;
        if (!n || n>src.size()-i) return false;
        auto a=src.subspan(i+1,n-1);
        bool skip=false;
        if(op==17 && a.size()==1) {
            // Capabilities corresponding exclusively to the dead narrow/wide
            // declarations in these exact two modules.
            switch(a[0]) {case 11:case 22:case 39:case 4433:case 4448:skip=true;break;}
        } else if ((op==21 || op==5) && !a.empty()) {
            skip=(a[0]>=4 && a[0]<=7)||a[0]==11;
        } else if (op==43 && a.size()>=3) {
            skip=(a[1]>=26 && a[1]<=28)||a[1]==32||a[1]==33;
        }
        if(skip){i+=n;continue;}
        if(wide_vertex && op==54) {
            emit(43,{10,mask,65535}); emit(43,{10,shift,16});
        }
        if(wide_vertex && (op==113||op==132||op==128) && a.size()>=3 &&
            a[0]==11 && a[1]>=447 && a[1]<=450) {
            ++wide;i+=n;continue;
        }
        if(wide_vertex && op==124 && a.size()==3 && a[0]==14 && a[1]==451 && a[2]==450) {
            product(441,446,14,451);++converted;i+=n;continue;
        }
        if(wide_vertex && op==151 && a.size()==4 && a[0]==42) {
            product(a[2],a[3],a[0],a[1]);++extended;i+=n;continue;
        }
        out.insert(out.end(),src.begin()+i,src.begin()+i+n);i+=n;
    }
    if(wide_vertex && (extended!=6 || wide!=4 || converted!=1))return false;
    out[3]=wide_vertex?next:src[3];
    dst=std::move(out);
    return true;
}
// gow3 19 Oct 2026 (#40, AMD Radeon RX 9070 XT): a graphics pipeline the driver refused
// (ErrorInitializationFailed) stopped every game at start. It is now created again without the
// pipeline cache and pipeline binaries; refused again, its draws are skipped and it is logged.
// GOW3_TEST_PIPELINE_FAIL=<n> (tests): the n-th graphics pipeline's first attempt fails as if
// refused; -<n>: both attempts.
bool TestPipelineRefusal(bool retry) {
    static const long test = [] {
        const char* env = std::getenv("GOW3_TEST_PIPELINE_FAIL");
        return env ? std::atol(env) : 0L;
    }();
    static std::atomic<long> created{0};
    static thread_local long current = 0;
    if (!test) {
        return false;
    }
    if (!retry) {
        current = ++created;
    }
    return current == (test > 0 ? test : -test) && (!retry || test < 0);
}

vk::UniquePipeline CreateRefusedAgain(vk::Device device, const vk::GraphicsPipelineCreateInfo& info,
                                      vk::Result first, u64 key_hash, const char* what) {
    auto result = device.createGraphicsPipelineUnique(nullptr, info);
    if (TestPipelineRefusal(true)) {
        result.result = vk::Result::eErrorInitializationFailed;
        result.value.reset();
    }
    static std::atomic<u32> logged{0};
    if (logged.fetch_add(1, std::memory_order_relaxed) < 16) {
        std::printf("GPU: the driver refused %s %#llx (%s); %s\n", what,
                    static_cast<unsigned long long>(key_hash), vk::to_string(first).c_str(),
                    result.result == vk::Result::eSuccess
                        ? "created without the pipeline cache"
                        : "refused again: its draws are skipped");
        std::fflush(stdout);
    }
    return result.result == vk::Result::eSuccess ? std::move(result.value) : vk::UniquePipeline{};
}

u64 NsSince(Clock::time_point start) {
    return u64(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count());
}
} // namespace

static constexpr std::array LogicalStageToStageBit = {
    vk::ShaderStageFlagBits::eFragment,
    vk::ShaderStageFlagBits::eTessellationControl,
    vk::ShaderStageFlagBits::eTessellationEvaluation,
    vk::ShaderStageFlagBits::eVertex,
    vk::ShaderStageFlagBits::eGeometry,
    vk::ShaderStageFlagBits::eCompute,
};

GraphicsPipeline::GraphicsPipeline(
    const Instance& instance, Scheduler& scheduler, DescriptorHeap& desc_heap,
    const Shader::Profile& profile, const GraphicsPipelineKey& key_,
    vk::PipelineCache pipeline_cache, std::span<const Shader::Info*, MaxShaderStages> infos,
    std::span<const Shader::RuntimeInfo, MaxShaderStages> runtime_infos,
    std::optional<const Shader::Gcn::FetchShaderData> fetch_shader_,
    std::span<const vk::ShaderModule> modules, SerializationSupport& sdata, bool preloading)
    : Pipeline{instance, scheduler, desc_heap, profile, pipeline_cache}, key{key_},
      fetch_shader{std::move(fetch_shader_)} {
    const auto started = Clock::now();
    const vk::Device device = instance.GetDevice();
    std::ranges::copy(infos, stages.begin());
    RefreshTextureHelperEligibility();
    BuildDescSetLayout(preloading);
    const auto debug_str = GetDebugString();

    const vk::PushConstantRange push_constants = {
        .stageFlags = AllGraphicsStageBits,
        .offset = 0,
        .size = sizeof(Shader::PushData),
    };

    const vk::DescriptorSetLayout set_layout = *desc_layout;
    const vk::PipelineLayoutCreateInfo layout_info = {
        .setLayoutCount = 1U,
        .pSetLayouts = &set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_constants,
    };
    auto [layout_result, layout] = instance.GetDevice().createPipelineLayoutUnique(layout_info);
    ASSERT_MSG(layout_result == vk::Result::eSuccess,
               "Failed to create graphics pipeline layout: {}", vk::to_string(layout_result));
    pipeline_layout = std::move(layout);
    SetObjectName(device, *pipeline_layout, "Graphics PipelineLayout {}", debug_str);

    // Buffer-only depth/geometry passes use the same compact per-draw
    // descriptor payload as compute. Mixed image/sampler layouts retain
    // their existing path. Count the immutable stage layout once.
    u32 template_buffers = 0;
    bool buffer_only_layout = true;
    for (const auto* stage : GetStages()) {
        if (!stage) continue;
        template_buffers += static_cast<u32>(stage->buffers.size());
        buffer_only_layout &= stage->images.empty() && stage->samplers.empty();
    }
    if (buffer_only_layout) {
        InitializeBufferDescriptorTemplate(template_buffers);
    }

    if (!preloading) {
        VertexInputs<AmdGpu::Buffer> guest_buffers;
        if (!instance.IsVertexInputDynamicState()) {
            const auto& vs_info = runtime_infos[u32(Shader::SwStage::Vertex)].sw.vs;
            GetVertexInputs(sdata.vertex_attributes, sdata.vertex_bindings, sdata.divisors,
                            guest_buffers, vs_info.step_rate_0, vs_info.step_rate_1);
        }
    }

    const vk::PipelineVertexInputDivisorStateCreateInfo divisor_state = {
        .vertexBindingDivisorCount = static_cast<u32>(sdata.divisors.size()),
        .pVertexBindingDivisors = sdata.divisors.data(),
    };

    const vk::PipelineVertexInputStateCreateInfo vertex_input_info = {
        .pNext = sdata.divisors.empty() ? nullptr : &divisor_state,
        .vertexBindingDescriptionCount = static_cast<u32>(sdata.vertex_bindings.size()),
        .pVertexBindingDescriptions = sdata.vertex_bindings.data(),
        .vertexAttributeDescriptionCount = static_cast<u32>(sdata.vertex_attributes.size()),
        .pVertexAttributeDescriptions = sdata.vertex_attributes.data(),
    };

    const auto topology = LiverpoolToVK::PrimitiveType(key.prim_type);
    const vk::PipelineInputAssemblyStateCreateInfo input_assembly = {
        .topology = topology,
    };

    const bool is_rect_list = key.prim_type == AmdGpu::PrimitiveType::RectList;
    const bool is_quad_list = key.prim_type == AmdGpu::PrimitiveType::QuadList;
    const vk::PipelineTessellationStateCreateInfo tessellation_state = {
        .patchControlPoints = is_rect_list ? 3U : (is_quad_list ? 4U : key.patch_control_points),
    };

    vk::StructureChain raster_chain = {
        vk::PipelineRasterizationStateCreateInfo{
            .depthClampEnable = key.depth_clamp_enable &&
                                (!key.depth_clip_enable || instance.IsDepthClipEnableSupported()),
            .rasterizerDiscardEnable = false,
            .polygonMode = LiverpoolToVK::PolygonMode(key.polygon_mode),
            .lineWidth = 1.0f,
        },
        vk::PipelineRasterizationProvokingVertexStateCreateInfoEXT{
            .provokingVertexMode = key.provoking_vtx_last == AmdGpu::ProvokingVtxLast::First
                                       ? vk::ProvokingVertexModeEXT::eFirstVertex
                                       : vk::ProvokingVertexModeEXT::eLastVertex,
        },
        vk::PipelineRasterizationDepthClipStateCreateInfoEXT{
            .depthClipEnable = key.depth_clip_enable,
        },
    };

    if (!instance.IsProvokingVertexSupported()) {
        raster_chain.unlink<vk::PipelineRasterizationProvokingVertexStateCreateInfoEXT>();
    }
    if (!instance.IsDepthClipEnableSupported()) {
        raster_chain.unlink<vk::PipelineRasterizationDepthClipStateCreateInfoEXT>();
    }

    if (!preloading) {
        const auto& fs_info = runtime_infos[u32(Shader::SwStage::Fragment)].hw.fs;
        sdata.multisampling = {
            .rasterizationSamples = LiverpoolToVK::NumSamples(
                key.num_samples, instance.GetColorSampleCounts() & instance.GetDepthSampleCounts()),
            .sampleShadingEnable =
                fs_info.addr_flags.persp_sample_ena || fs_info.addr_flags.linear_sample_ena,
        };
    }

    const vk::PipelineViewportDepthClipControlCreateInfoEXT clip_control = {
        .negativeOneToOne = key.clip_space == AmdGpu::ClipSpace::MinusWToW,
    };

    const vk::PipelineViewportStateCreateInfo viewport_info = {
        .pNext = instance.IsDepthClipControlSupported() ? &clip_control : nullptr,
    };

    boost::container::static_vector<vk::DynamicState, 32> dynamic_states = {
        vk::DynamicState::eViewportWithCount,  vk::DynamicState::eScissorWithCount,
        vk::DynamicState::eBlendConstants,     vk::DynamicState::eDepthTestEnable,
        vk::DynamicState::eDepthWriteEnable,   vk::DynamicState::eDepthCompareOp,
        vk::DynamicState::eDepthBiasEnable,    vk::DynamicState::eDepthBias,
        vk::DynamicState::eStencilTestEnable,  vk::DynamicState::eStencilReference,
        vk::DynamicState::eStencilCompareMask, vk::DynamicState::eStencilWriteMask,
        vk::DynamicState::eStencilOp,          vk::DynamicState::eCullMode,
        vk::DynamicState::eFrontFace,          vk::DynamicState::eRasterizerDiscardEnable,
        vk::DynamicState::eLineWidth,          vk::DynamicState::ePrimitiveRestartEnable,
    };

    if (instance.IsDepthBoundsSupported()) {
        dynamic_states.push_back(vk::DynamicState::eDepthBoundsTestEnable);
        dynamic_states.push_back(vk::DynamicState::eDepthBounds);
    }
    if (instance.IsDynamicColorWriteMaskSupported()) {
        dynamic_states.push_back(vk::DynamicState::eColorWriteMaskEXT);
    }
    if (instance.IsVertexInputDynamicState()) {
        dynamic_states.push_back(vk::DynamicState::eVertexInputEXT);
    } else if (!sdata.vertex_bindings.empty()) {
        dynamic_states.push_back(vk::DynamicState::eVertexInputBindingStride);
    }

    const vk::PipelineDynamicStateCreateInfo dynamic_info = {
        .dynamicStateCount = static_cast<u32>(dynamic_states.size()),
        .pDynamicStates = dynamic_states.data(),
    };

    boost::container::static_vector<vk::PipelineShaderStageCreateInfo, MaxShaderStages>
        shader_stages;
    auto stage = u32(Shader::SwStage::Vertex);
    if (infos[stage]) {
        shader_stages.emplace_back(vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eVertex,
            .module = modules[stage],
            .pName = "main",
        });
    }
    stage = u32(Shader::SwStage::Geometry);
    if (infos[stage]) {
        shader_stages.emplace_back(vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eGeometry,
            .module = modules[stage],
            .pName = "main",
        });
    }
    stage = u32(Shader::SwStage::TessellationControl);
    if (infos[stage]) {
        shader_stages.emplace_back(vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eTessellationControl,
            .module = modules[stage],
            .pName = "main",
        });
    } else if (is_rect_list || is_quad_list) {
        const auto type = is_quad_list ? AuxShaderType::QuadListTCS : AuxShaderType::RectListTCS;
        if (!preloading) {
            const auto& fs_info = runtime_infos[u32(Shader::SwStage::Fragment)].hw.fs;
            sdata.tcs = Shader::Backend::SPIRV::EmitAuxilaryTessShader(type, fs_info);
        }
        shader_stages.emplace_back(vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eTessellationControl,
            .module = CompileSPV(sdata.tcs, instance.GetDevice()),
            .pName = "main",
        });
    }
    stage = u32(Shader::SwStage::TessellationEval);
    if (infos[stage]) {
        shader_stages.emplace_back(vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eTessellationEvaluation,
            .module = modules[stage],
            .pName = "main",
        });
    } else if (is_rect_list || is_quad_list) {
        if (!preloading) {
            const auto& fs_info = runtime_infos[u32(Shader::SwStage::Fragment)].hw.fs;
            sdata.tes = Shader::Backend::SPIRV::EmitAuxilaryTessShader(
                AuxShaderType::PassthroughTES, fs_info);
        }
        shader_stages.emplace_back(vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eTessellationEvaluation,
            .module = CompileSPV(sdata.tes, instance.GetDevice()),
            .pName = "main",
        });
    }
    stage = u32(Shader::SwStage::Fragment);
    if (infos[stage]) {
        shader_stages.emplace_back(vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eFragment,
            .module = modules[stage],
            .pName = "main",
        });
    } else if (runtime_infos[u32(Shader::SwStage::Fragment)].hw.fs.clip_distance_emulation) {
        if (!preloading) {
            const auto& vs = runtime_infos[static_cast<u32>(Shader::SwStage::Vertex)].hw.vs;

            sdata.fragment = Shader::Backend::SPIRV::EmitDiscardFragmentShader(vs.outputs);
        }
        shader_stages.emplace_back(vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eFragment,
            .module = CompileSPV(sdata.fragment, instance.GetDevice()),
            .pName = "main",
        });
    }

    const vk::PipelineShaderStageRequiredSubgroupSizeCreateInfo subgroup_size_ci = {
        .requiredSubgroupSize = 64,
    };
    for (auto& stage : shader_stages) {
        // Qualcomm may support subgroup size control in compute but not
        // fragment/vertex. Do not submit invalid required-size stage chains.
        const bool require64=instance.GetDriverID()==vk::DriverId::eQualcommProprietary
            ? instance.CanRequireSubgroup64(stage.stage)
            : instance.IsSubgroupSize64Supported();
        stage.pNext = require64 ? &subgroup_size_ci : nullptr;
    }

    const auto depth_format =
        instance.GetSupportedFormat(LiverpoolToVK::DepthFormat(key.z_format, key.stencil_format),
                                    vk::FormatFeatureFlagBits2::eDepthStencilAttachment);
    std::array<vk::Format, Shader::IR::NumRenderTargets> color_formats;
    for (s32 i = 0; i < key.num_color_attachments; ++i) {
        const auto& col_buf = key.color_buffers[i];
        const auto format = LiverpoolToVK::SurfaceFormat(col_buf.data_format, col_buf.num_format);
        const auto color_format =
            instance.GetSupportedFormat(format, vk::FormatFeatureFlagBits2::eColorAttachment);
        if (!instance.IsFormatSupported(color_format,
                                        vk::FormatFeatureFlagBits2::eColorAttachment)) {
            LOG_WARNING(Render_Vulkan,
                        "color buffer format {} does not support COLOR_ATTACHMENT_BIT",
                        vk::to_string(color_format));
        }
        color_formats[i] = color_format;
    }

    std::array<vk::SampleCountFlagBits, AmdGpu::NUM_COLOR_BUFFERS> color_samples;
    std::ranges::transform(key.color_samples, color_samples.begin(), [&instance](u8 num_samples) {
        return num_samples ? LiverpoolToVK::NumSamples(num_samples, instance.GetColorSampleCounts())
                           : vk::SampleCountFlagBits::e1;
    });
    const vk::AttachmentSampleCountInfoAMD mixed_samples = {
        .colorAttachmentCount = key.num_color_attachments,
        .pColorAttachmentSamples = color_samples.data(),
        .depthStencilAttachmentSamples =
            LiverpoolToVK::NumSamples(key.depth_samples, instance.GetDepthSampleCounts()),
    };

    const vk::PipelineRenderingCreateInfo pipeline_rendering_ci = {
        .pNext = instance.IsMixedDepthSamplesSupported() ? &mixed_samples : nullptr,
        .colorAttachmentCount = key.num_color_attachments,
        .pColorAttachmentFormats = color_formats.data(),
        .depthAttachmentFormat = key.z_format != AmdGpu::DepthBuffer::ZFormat::Invalid
                                     ? depth_format
                                     : vk::Format::eUndefined,
        .stencilAttachmentFormat = key.stencil_format != AmdGpu::DepthBuffer::StencilFormat::Invalid
                                       ? depth_format
                                       : vk::Format::eUndefined,
    };

    std::array<vk::PipelineColorBlendAttachmentState, AmdGpu::NUM_COLOR_BUFFERS> attachments;
    // gow3: GNM scales MIN/MAX operands, Vulkan does not (vk_blend_rewrite.h). A self-scaled
    // MIN/MAX is drawn twice: native, then this pipeline squares the destination.
    std::array<vk::PipelineColorBlendAttachmentState, AmdGpu::NUM_COLOR_BUFFERS> square_attachments;
    bool needs_square_pass = false;
    for (u32 i = 0; i < key.num_color_attachments; i++) {
        const auto& control = key.blend_controls[i];
        const auto target_format = key.color_buffers[i].num_format;
        BlendEquation color_equation{control.color_src_factor, control.color_func,
                                     control.color_dst_factor};
        BlendEquation alpha_equation{
            control.separate_alpha_blend ? control.alpha_src_factor : control.color_src_factor,
            control.separate_alpha_blend ? control.alpha_func : control.color_func,
            control.separate_alpha_blend ? control.alpha_dst_factor : control.color_dst_factor};
        const auto color_rewrite =
            RewriteScaledMinMaxBlend(color_equation, target_format, BlendChannel::Color);
        const auto alpha_rewrite =
            RewriteScaledMinMaxBlend(alpha_equation, target_format, BlendChannel::Alpha);
        const bool writes_color = bool(key.write_masks[i] & (vk::ColorComponentFlagBits::eR |
                                                             vk::ColorComponentFlagBits::eG |
                                                             vk::ColorComponentFlagBits::eB));
        const bool writes_alpha = bool(key.write_masks[i] & vk::ColorComponentFlagBits::eA);
        if (control.enable &&
            ((writes_color && color_rewrite == BlendRewriteResult::Unsupported) ||
             (writes_alpha && alpha_rewrite == BlendRewriteResult::Unsupported))) {
            LOG_WARNING(Render_Vulkan,
                        "Scaled MIN/MAX blend on attachment {} has no Vulkan equivalent "
                        "(format {}, color {}/{}/{}, alpha {}/{}/{})",
                        i, u32(target_format), u32(color_equation.src_factor),
                        u32(color_equation.function), u32(color_equation.dst_factor),
                        u32(alpha_equation.src_factor), u32(alpha_equation.function),
                        u32(alpha_equation.dst_factor));
        }
        const bool square_color =
            control.enable && writes_color && color_rewrite == BlendRewriteResult::Squared;
        const bool square_alpha =
            control.enable && writes_alpha && alpha_rewrite == BlendRewriteResult::Squared;
        needs_square_pass |= square_color || square_alpha;

        const auto src_color = LiverpoolToVK::BlendFactor(color_equation.src_factor);
        const auto dst_color = LiverpoolToVK::BlendFactor(color_equation.dst_factor);

        attachments[i] = vk::PipelineColorBlendAttachmentState{
            .blendEnable = control.enable,
            .srcColorBlendFactor = src_color,
            .dstColorBlendFactor = dst_color,
            .colorBlendOp = LiverpoolToVK::BlendOp(color_equation.function),
            .srcAlphaBlendFactor = LiverpoolToVK::BlendFactor(alpha_equation.src_factor),
            .dstAlphaBlendFactor = LiverpoolToVK::BlendFactor(alpha_equation.dst_factor),
            .alphaBlendOp = LiverpoolToVK::BlendOp(alpha_equation.function),
            .colorWriteMask =
                instance.IsDynamicColorWriteMaskSupported()
                    ? vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
                    : key.write_masks[i],
        };
        square_attachments[i] = vk::PipelineColorBlendAttachmentState{
            .blendEnable = true,
            .srcColorBlendFactor = vk::BlendFactor::eZero,
            .dstColorBlendFactor = square_color ? vk::BlendFactor::eDstColor : vk::BlendFactor::eOne,
            .colorBlendOp = vk::BlendOp::eAdd,
            .srcAlphaBlendFactor = vk::BlendFactor::eZero,
            .dstAlphaBlendFactor = square_alpha ? vk::BlendFactor::eDstAlpha : vk::BlendFactor::eOne,
            .alphaBlendOp = vk::BlendOp::eAdd,
            .colorWriteMask = attachments[i].colorWriteMask,
        };

        // On GCN GPU there is an additional mask which allows to control color components exported
        // from a pixel shader. A situation possible, when the game may mask out the alpha channel,
        // while it is still need to be used in blending ops. For such cases, HW will default alpha
        // to 1 and perform the blending, while shader normally outputs 0 in the last component.
        // Unfortunatelly, Vulkan doesn't provide any control on blend inputs, so below we detecting
        // such cases and override alpha value in order to emulate HW behaviour.
        const auto has_alpha_masked_out =
            (key.cb_shader_mask.GetMask(i) & AmdGpu::ColorBufferMask::ComponentA) == 0;
        const auto has_src_alpha_in_src_blend = src_color == vk::BlendFactor::eSrcAlpha ||
                                                src_color == vk::BlendFactor::eOneMinusSrcAlpha;
        const auto has_src_alpha_in_dst_blend = dst_color == vk::BlendFactor::eSrcAlpha ||
                                                dst_color == vk::BlendFactor::eOneMinusSrcAlpha;
        if (has_alpha_masked_out && has_src_alpha_in_src_blend) {
            attachments[i].srcColorBlendFactor = src_color == vk::BlendFactor::eSrcAlpha
                                                     ? vk::BlendFactor::eOne
                                                     : vk::BlendFactor::eZero; // 1-A
        }
        if (has_alpha_masked_out && has_src_alpha_in_dst_blend) {
            attachments[i].dstColorBlendFactor = dst_color == vk::BlendFactor::eSrcAlpha
                                                     ? vk::BlendFactor::eOne
                                                     : vk::BlendFactor::eZero; // 1-A
        }
    }

    const vk::PipelineColorBlendStateCreateInfo color_blending = {
        .logicOpEnable =
            instance.IsLogicOpSupported() && key.logic_op != AmdGpu::ColorControl::LogicOp::Copy,
        .logicOp = LiverpoolToVK::LogicOp(key.logic_op),
        .attachmentCount = key.num_color_attachments,
        .pAttachments = attachments.data(),
        .blendConstants = std::array{1.0f, 1.0f, 1.0f, 1.0f},
    };

    // Required by spec unless VK_EXT_extended_dynamic_state3 is supported.
    // In practice, we use dynamic state for all of it.
    constexpr vk::PipelineDepthStencilStateCreateInfo depth_stencil_info = {};

    const vk::GraphicsPipelineCreateInfo pipeline_info = {
        .pNext = &pipeline_rendering_ci,
        .stageCount = static_cast<u32>(shader_stages.size()),
        .pStages = shader_stages.data(),
        .pVertexInputState = !instance.IsVertexInputDynamicState() ? &vertex_input_info : nullptr,
        .pInputAssemblyState = &input_assembly,
        .pTessellationState = &tessellation_state,
        .pViewportState = &viewport_info,
        .pRasterizationState = &raster_chain.get(),
        .pMultisampleState = &sdata.multisampling,
        .pDepthStencilState =
            !instance.IsExtendedDynamicState3Supported() ? &depth_stencil_info : nullptr,
        .pColorBlendState = &color_blending,
        .pDynamicState = &dynamic_info,
        .layout = *pipeline_layout,
    };

    const auto binary_directory = Common::FS::GetUserPath(Common::FS::PathType::CacheDir) /
        Common::ElfInfo::Instance().GameSerial() / "pipeline-binaries";
    auto [pipeline_result, pipe] =
        CreateCachedGraphicsPipeline(device, pipeline_cache, pipeline_info, key,
                                     instance.GetPipelineBinaryGlobalKey(), binary_directory);
    if (TestPipelineRefusal(false)) {
        pipeline_result = vk::Result::eErrorInitializationFailed;
        pipe.reset();
    }
    const u64 key_hash = std::hash<GraphicsPipelineKey>{}(key);
    if (pipeline_result != vk::Result::eSuccess) {
        pipe = CreateRefusedAgain(device, pipeline_info, pipeline_result, key_hash, "graphics pipeline");
    }
    // Keep replacement module handles alive through the optional square pass.
    std::vector<vk::UniqueShaderModule> recovered_modules;
    // Retry the exact same rendering state without driver optimizer passes.
    // With a native subgroup of 64, explicit 64 is redundant for graphics.
    if (!pipe && pipeline_result==vk::Result::eErrorUnknown &&
        instance.GetDriverID()==vk::DriverId::eQualcommProprietary) {
        auto compat=pipeline_info;
        auto stages_compat=shader_stages;
        if (instance.SubgroupSize()==64)
            for (auto& stage:stages_compat)stage.pNext=nullptr;
        compat.pStages=stages_compat.data();
        auto [rr,rp]=device.createGraphicsPipelineUnique(nullptr,compat);
        if(rr!=vk::Result::eSuccess) {
            compat.flags|=vk::PipelineCreateFlagBits::eDisableOptimization;
            auto retry=device.createGraphicsPipelineUnique(nullptr,compat);
            rr=retry.result;rp=std::move(retry.value);
        }
        if(rr!=vk::Result::eSuccess) {
            constexpr auto vs=u32(Shader::SwStage::Vertex);
            constexpr auto fs=u32(Shader::SwStage::Fragment);
            // Exact byte identities, not guest hashes, authorize the lowering.
            if(infos[vs] && infos[fs]) {
                std::vector<u32> vs_src,fs_src,vs_lower,fs_lower;
                const auto get=[&](u32 stage,std::vector<u32>& words) {
                    return CopyShaderModuleSource(device,modules[stage],words) ||
                           LoadExactComputeSpirv(infos[stage]->pgm_hash,key.stage_hashes[stage],words);
                };
                if(get(vs,vs_src) && get(fs,fs_src) &&
                   LowerCapturedQualcommGraphics(vs_src,vs_lower,true) &&
                   LowerCapturedQualcommGraphics(fs_src,fs_lower,false)) {
                    auto make=[&](const std::vector<u32>& words) {
                        return device.createShaderModuleUnique(vk::ShaderModuleCreateInfo{
                            .codeSize=words.size()*sizeof(u32),.pCode=words.data()});
                    };
                    auto v=make(vs_lower), f=make(fs_lower);
                    if(v.result==vk::Result::eSuccess && f.result==vk::Result::eSuccess) {
                        for(auto& stage:stages_compat) {
                            if(stage.stage==vk::ShaderStageFlagBits::eVertex)stage.module=*v.value;
                            if(stage.stage==vk::ShaderStageFlagBits::eFragment)stage.module=*f.value;
                        }
                        // Prefer optimized code: the rewrite is algebraically
                        // exact and removes the wide ALU compiler failure mode.
                        compat.flags=pipeline_info.flags;
                        // The normalized shader has its own Vulkan driver key.
                        // Save/restore only binaries matching this actual create
                        // info, never alias the original rejected pipeline.
                        auto retry=CreateCachedGraphicsPipeline(device,pipeline_cache,compat,key,
                            instance.GetPipelineBinaryGlobalKey(),binary_directory);
                        if(retry.result!=vk::Result::eSuccess) {
                            compat.flags|=vk::PipelineCreateFlagBits::eDisableOptimization;
                            retry=CreateCachedGraphicsPipeline(device,pipeline_cache,compat,key,
                                instance.GetPipelineBinaryGlobalKey(),binary_directory);
                        }
                        rr=retry.result;rp=std::move(retry.value);
                        if(rr==vk::Result::eSuccess) {
                            recovered_modules.push_back(std::move(v.value));
                            recovered_modules.push_back(std::move(f.value));
                            // pipeline_info points to this stable array. The
                            // same exact variants must be used by square pass.
                            std::ranges::copy(stages_compat,shader_stages.begin());
                            std::printf("Qualcomm graphics recovered: verified dead declarations or exact ALU lowering VS/PS key=%016llx\n",
                                static_cast<unsigned long long>(key_hash));
                        }
                    }
                }
            }
        }
        if(rr==vk::Result::eSuccess) {
            pipe=std::move(rp);
            std::printf("Qualcomm graphics recovered: unchanged shader/state key=%016llx\n",
                static_cast<unsigned long long>(key_hash));
        } else {
            std::printf("Qualcomm graphics still refused: key=%016llx result=%s\n",
                static_cast<unsigned long long>(key_hash),vk::to_string(rr).c_str());
            // Capture exact rejected permutations, not arbitrary binaries with
            // matching guest hashes. Keep capture small and serialize writers.
            static std::mutex capture_mutex;
            static unsigned captures=0;
            std::scoped_lock lock{capture_mutex};
            if(captures<12) if(const char* root=std::getenv("GOW3_GPU_USER_DIR");root&&*root) {
                const unsigned slot=captures++;
                const std::filesystem::path dir(root);
                std::ofstream report(dir/("qualcomm-graphics-"+std::to_string(slot)+".txt"));
                report<<"key="<<std::hex<<key_hash<<" native_subgroup="<<std::dec<<instance.SubgroupSize()
                      <<" colors="<<unsigned(key.num_color_attachments)<<" depth="<<unsigned(key.z_format)
                      <<" stencil="<<unsigned(key.stencil_format)<<"\n";
                for(size_t i=0;i<MaxShaderStages;++i)if(infos[i]) {
                    std::vector<u32> spv;
                    const bool ok=CopyShaderModuleSource(device,modules[i],spv)||
                        LoadExactComputeSpirv(infos[i]->pgm_hash,key.stage_hashes[i],spv);
                    report<<"stage="<<i<<" guest_hash="<<std::hex<<infos[i]->pgm_hash
                          <<" permutation="<<key.stage_hashes[i]<<" bytes="<<std::dec<<(ok?spv.size()*4:0)<<"\n";
                    if(ok) {
                        std::ofstream out(dir/("qualcomm-graphics-"+std::to_string(slot)+"-"+std::to_string(i)+".spv"),std::ios::binary);
                        out.write(reinterpret_cast<const char*>(spv.data()),std::streamsize(spv.size()*4));
                    }
                }
            }
        }
    }
    pipeline = std::move(pipe);
    if (!pipeline) {
        g_gow3_pipeline_total_ns += NsSince(started);
        return; // Failed(): its draws are skipped
    }
    SetObjectName(device, *pipeline, "Graphics Pipeline {}", debug_str);

    if (needs_square_pass) {
        auto square_blending = color_blending;
        square_blending.logicOpEnable = false;
        square_blending.pAttachments = square_attachments.data();
        auto square_info = pipeline_info;
        square_info.pColorBlendState = &square_blending;
        auto [square_result, square] =
            CreateCachedGraphicsPipeline(device, pipeline_cache, square_info, key,
                                         instance.GetPipelineBinaryGlobalKey(), binary_directory);
        if (square_result != vk::Result::eSuccess) {
            square = CreateRefusedAgain(device, square_info, square_result, key_hash,
                                        "squaring pipeline");
        }
        if (!square) {
            pipeline.reset(); // Failed(): the draws need both
            g_gow3_pipeline_total_ns += NsSince(started);
            return;
        }
        square_pipeline = std::move(square);
        SetObjectName(device, *square_pipeline, "Graphics Pipeline {} squaring pass", debug_str);
    }
    g_gow3_pipeline_total_ns += NsSince(started);
    ++g_gow3_pipelines_built;
}

GraphicsPipeline::~GraphicsPipeline() = default;

template <typename Attribute, typename Binding>
void GraphicsPipeline::GetVertexInputs(
    VertexInputs<Attribute>& attributes, VertexInputs<Binding>& bindings,
    VertexInputs<vk::VertexInputBindingDivisorDescriptionEXT>& divisors,
    VertexInputs<AmdGpu::Buffer>& guest_buffers, u32 step_rate_0, u32 step_rate_1) const {
    using InstanceIdType = Shader::Gcn::VertexAttribute::InstanceIdType;
    if (!fetch_shader || fetch_shader->attributes.empty()) {
        return;
    }
    const auto& vs_info = GetStage(Shader::SwStage::Vertex);
    for (const auto& attrib : fetch_shader->attributes) {
        const auto step_rate = attrib.GetStepRate();
        const auto buffer = attrib.GetSharp(vs_info);
        attributes.push_back(Attribute{
            .location = attrib.semantic,
            .binding = attrib.semantic,
            .format = LiverpoolToVK::SurfaceFormat(buffer.GetDataFmt(), buffer.GetNumberFmt()),
            .offset = 0,
        });
        bindings.push_back(Binding{
            .binding = attrib.semantic,
            .stride = buffer.GetStride(),
            .inputRate = step_rate == InstanceIdType::None ? vk::VertexInputRate::eVertex
                                                           : vk::VertexInputRate::eInstance,
        });
        const u32 divisor = step_rate == InstanceIdType::OverStepRate0
                                ? step_rate_0
                                : (step_rate == InstanceIdType::OverStepRate1 ? step_rate_1 : 1);
        if constexpr (std::is_same_v<Binding, vk::VertexInputBindingDescription2EXT>) {
            bindings.back().divisor = divisor;
        } else if (step_rate != InstanceIdType::None) {
            divisors.push_back(vk::VertexInputBindingDivisorDescriptionEXT{
                .binding = attrib.semantic,
                .divisor = divisor,
            });
        }
        guest_buffers.emplace_back(buffer);
    }
}

// Declare templated GetVertexInputs for necessary types.
template void GraphicsPipeline::GetVertexInputs(
    VertexInputs<vk::VertexInputAttributeDescription>& attributes,
    VertexInputs<vk::VertexInputBindingDescription>& bindings,
    VertexInputs<vk::VertexInputBindingDivisorDescriptionEXT>& divisors,
    VertexInputs<AmdGpu::Buffer>& guest_buffers, u32 step_rate_0, u32 step_rate_1) const;
template void GraphicsPipeline::GetVertexInputs(
    VertexInputs<vk::VertexInputAttributeDescription2EXT>& attributes,
    VertexInputs<vk::VertexInputBindingDescription2EXT>& bindings,
    VertexInputs<vk::VertexInputBindingDivisorDescriptionEXT>& divisors,
    VertexInputs<AmdGpu::Buffer>& guest_buffers, u32 step_rate_0, u32 step_rate_1) const;

void GraphicsPipeline::BuildDescSetLayout(bool preloading) {
    boost::container::small_vector<vk::DescriptorSetLayoutBinding, 32> bindings;
    u32 binding{};

    for (const auto* stage : stages) {
        if (!stage) {
            continue;
        }
        const auto stage_bit = LogicalStageToStageBit[u32(stage->sw_stage)];
        for (const auto& buffer : stage->buffers) {
            const auto sharp =
                preloading ? AmdGpu::Buffer{}
                           : buffer.GetSharp(*stage); // See for the comment in compute PL creation
            bindings.push_back({
                .binding = binding++,
                .descriptorType = vk::DescriptorType::eStorageBuffer,
                .descriptorCount = 1,
                .stageFlags = stage_bit,
            });
        }
        for (const auto& image : stage->images) {
            const u32 num_bindings = image.NumBindings(*stage);
            bindings.push_back({
                .binding = binding,
                .descriptorType = image.is_written ? vk::DescriptorType::eStorageImage
                                                   : vk::DescriptorType::eSampledImage,
                .descriptorCount = num_bindings,
                .stageFlags = stage_bit,
            });
            binding += num_bindings;
        }
        for (const auto& sampler : stage->samplers) {
            bindings.push_back({
                .binding = binding++,
                .descriptorType = vk::DescriptorType::eSampler,
                .descriptorCount = 1,
                .stageFlags = stage_bit,
            });
        }
    }
    SelectDescriptorLayout(binding, std::span<const vk::DescriptorSetLayoutBinding>{bindings.data(),bindings.size()});
    const auto flags = uses_push_descriptors
                           ? vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR
                           : vk::DescriptorSetLayoutCreateFlagBits{};
    const vk::DescriptorSetLayoutCreateInfo desc_layout_ci = {
        .flags = flags,
        .bindingCount = static_cast<u32>(bindings.size()),
        .pBindings = bindings.data(),
    };
    auto [layout_result, layout] =
        instance.GetDevice().createDescriptorSetLayoutUnique(desc_layout_ci);
    ASSERT_MSG(layout_result == vk::Result::eSuccess,
               "Failed to create graphics descriptor set layout: {}", vk::to_string(layout_result));
    desc_layout = std::move(layout);
}

} // namespace Vulkan
