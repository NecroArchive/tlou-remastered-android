// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <bitset>

#include "common/types.h"
#include "shader_recompiler/backend/bindings.h"
#include "shader_recompiler/frontend/fetch_shader.h"
#include "shader_recompiler/info.h"
#include "shader_recompiler/profile.h"

namespace Shader {

struct VsAttribSpecialization {
    u32 divisor{};
    AmdGpu::NumberClass num_class{};
    AmdGpu::CompMapping dst_select{};

    bool operator==(const VsAttribSpecialization&) const = default;
};

struct BufferSpecialization {
    u32 stride : 14;
    u32 is_formatted : 1;
    u32 swizzle_enable : 1;
    u32 data_format : 6;
    u32 num_format : 4;
    u32 index_stride : 2;
    u32 element_size : 2;
    AmdGpu::CompMapping dst_select{};
    AmdGpu::NumberConversion num_conversion{};

    bool operator==(const BufferSpecialization& other) const {
        return stride == other.stride && is_formatted == other.is_formatted &&
               swizzle_enable == other.swizzle_enable &&
               (!is_formatted ||
                (data_format == other.data_format && num_format == other.num_format &&
                 dst_select == other.dst_select && num_conversion == other.num_conversion)) &&
               (!swizzle_enable ||
                (index_stride == other.index_stride && element_size == other.element_size));
    }
};

struct ImageSpecialization {
    AmdGpu::ImageType type = AmdGpu::ImageType::Color2D;
    bool is_integer = false;
    bool is_storage = false;
    bool is_cube = false;
    bool is_srgb = false;
    AmdGpu::CompMapping dst_select{};
    AmdGpu::NumberConversion num_conversion{};
    // FIXME any pipeline cache changes needed?
    u32 num_bindings = 0;

    bool operator==(const ImageSpecialization&) const = default;
};

struct FMaskSpecialization {
    u32 width;
    u32 height;

    bool operator==(const FMaskSpecialization&) const = default;
};

struct SamplerSpecialization {
    u8 force_unnormalized : 1;
    u8 force_degamma : 1;

    bool operator==(const SamplerSpecialization&) const = default;
};

/**
 * Alongside runtime information, this structure also checks bound resources
 * for compatibility. Can be used as a key for storing shader permutations.
 * Is separate from runtime information, because resource layout can only be deduced
 * after the first compilation of a module.
 */
struct StageSpecialization {
    static constexpr size_t MaxStageResources = 128;

    const Info* info{};
    RuntimeInfo runtime_info{};
    std::bitset<MaxStageResources> bitset{};
    std::optional<Gcn::FetchShaderData> fetch_shader_data{};
    boost::container::small_vector<VsAttribSpecialization, 32> vs_attribs;
    boost::container::small_vector<BufferSpecialization, 16> buffers;
    boost::container::small_vector<ImageSpecialization, 16> images;
    boost::container::small_vector<FMaskSpecialization, 8> fmasks;
    boost::container::small_vector<SamplerSpecialization, 16> samplers;
    Backend::Bindings start{};

    StageSpecialization() = default;
    StageSpecialization(const Info& info_, RuntimeInfo runtime_info_, const Profile& profile_,
                        Backend::Bindings start_)
        : info{&info_}, runtime_info{runtime_info_}, start{start_} {
        fetch_shader_data = Gcn::ParseFetchShader(info_);
        if (info_.sw_stage == SwStage::Vertex && fetch_shader_data) {
            // Specialize shader on VS input number types to follow spec.
            ForEachSharp(vs_attribs, fetch_shader_data->attributes,
                         [this](auto& spec, const auto& desc, AmdGpu::Buffer sharp) {
                             using InstanceIdType = Shader::Gcn::VertexAttribute::InstanceIdType;
                             if (const auto step_rate = desc.GetStepRate();
                                 step_rate != InstanceIdType::None) {
                                 spec.divisor = step_rate == InstanceIdType::OverStepRate0
                                                    ? runtime_info.sw.vs.step_rate_0
                                                    : (step_rate == InstanceIdType::OverStepRate1
                                                           ? runtime_info.sw.vs.step_rate_1
                                                           : 1);
                             }
                             spec.num_class = AmdGpu::GetNumberClass(sharp.GetNumberFmt());
                             spec.dst_select = sharp.DstSelect();
                         });
        }
        u32 binding{};
        ForEachSharp(binding, buffers, info->buffers,
                     [](auto& spec, const auto& desc, AmdGpu::Buffer sharp) {
                         spec.stride = sharp.GetStride();
                         spec.is_formatted = desc.is_formatted;
                         spec.swizzle_enable = sharp.swizzle_enable;
                         if (spec.is_formatted) {
                             spec.data_format = static_cast<u32>(sharp.GetDataFmt());
                             spec.num_format = static_cast<u32>(sharp.GetNumberFmt());
                             spec.dst_select = sharp.DstSelect();
                             spec.num_conversion = sharp.GetNumberConversion();
                         }
                         if (spec.swizzle_enable) {
                             spec.index_stride = sharp.index_stride;
                             spec.element_size = sharp.element_size;
                         }
                     });
        ForEachSharp(binding, images, info->images,
                     [&](auto& spec, const auto& desc, AmdGpu::Image sharp) {
                         spec.type = sharp.GetViewType(desc.is_array);
                         spec.is_integer = AmdGpu::IsInteger(sharp.GetNumberFmt());
                         spec.is_storage = desc.is_written;
                         spec.is_cube = sharp.IsCube();
                         if (spec.is_storage) {
                             spec.dst_select = sharp.DstSelect();
                         } else {
                             spec.is_srgb = sharp.GetNumberFmt() == AmdGpu::NumberFormat::Srgb;
                         }
                         spec.num_conversion = sharp.GetNumberConversion();
                         spec.num_bindings = desc.NumBindings(*info);
                     });
        ForEachSharp(binding, fmasks, info->fmasks,
                     [](auto& spec, const auto& desc, AmdGpu::Image sharp) {
                         spec.width = sharp.width;
                         spec.height = sharp.height;
                     });
        ForEachSharp(samplers, info->samplers,
                     [](auto& spec, const auto& desc, AmdGpu::Sampler sharp) {
                         spec.force_unnormalized = sharp.force_unnormalized;
                         spec.force_degamma = sharp.force_degamma;
                     });

        // Initialize runtime_info fields that rely on analysis in tessellation passes
        if (info->sw_stage == SwStage::TessellationControl ||
            info->sw_stage == SwStage::TessellationEval) {
            TessellationDataConstantBuffer tess_constants{};
            info->ReadTessConstantBuffer(tess_constants);
            runtime_info.InitFromTessConstants(tess_constants);
        }
    }


    // A directly comparable specialization for compute programs with buffers
    // only. The original constructor creates/fills several small_vectors and
    // the caller searches the permutation list on EVERY dispatch, even for a
    // module whose buffer formats and strides did not change. Re-read the
    // CURRENT per-dispatch SRT values, but compare their relevant specialization
    // fields directly to the last compiled module. It is a MISS for all
    // images/samplers/FMAsks/fetch/tessellation cases; those retain the full path.
    //
    // This intentionally mirrors operator==(cached, fresh), including its
    // use of FRESH resource-validity bits (not cached bitset) and its special
    // no-bindings start comparison. Never cache guest pointers or SRT bytes.
    [[nodiscard]] bool MatchesCurrentBufferOnlyCompute(
        const Info& current, const RuntimeInfo& current_runtime,
        const Backend::Bindings& current_start) const {
        if (!Valid() || current.sw_stage != SwStage::Compute ||
            current.has_fetch_shader || !current.images.empty() ||
            !current.samplers.empty() || !current.fmasks.empty() ||
            current.buffers.size() != buffers.size() || !images.empty() ||
            !samplers.empty() || !fmasks.empty() || fetch_shader_data ||
            !vs_attribs.empty() || runtime_info != current_runtime) {
            return false;
        }
        bool current_has_binding = false;
        for (u32 i = 0; i < current.buffers.size(); ++i) {
            const auto& desc = current.buffers[i];
            const auto sharp = desc.GetSharp(current);
            if (!sharp) {
                continue; // exactly as ForEachSharp's empty specialization
            }
            current_has_binding = true;
            BufferSpecialization fresh{};
            fresh.stride = sharp.GetStride();
            fresh.is_formatted = desc.is_formatted;
            fresh.swizzle_enable = sharp.swizzle_enable;
            if (fresh.is_formatted) {
                fresh.data_format = static_cast<u32>(sharp.GetDataFmt());
                fresh.num_format = static_cast<u32>(sharp.GetNumberFmt());
                fresh.dst_select = sharp.DstSelect();
                fresh.num_conversion = sharp.GetNumberConversion();
            }
            if (fresh.swizzle_enable) {
                fresh.index_stride = sharp.index_stride;
                fresh.element_size = sharp.element_size;
            }
            if (buffers[i] != fresh) {
                return false;
            }
        }
        // The original cached == freshly-built specialization ignores start
        // only when BOTH bitsets are empty. A changed buffer binding layout
        // must still trigger the complete Shader::StageSpecialization path.
        return (!current_has_binding && bitset.none()) || start == current_start;
    }


    // Guest data is re-read on every graphics draw. Avoid constructing
    // StageSpecialization's five resource vectors for common shaders when
    // the *semantic* Vulkan specialization is identical to the last module.
    // This is NOT a descriptor-value cache. Every field that influences the
    // module is compared against freshly fetched buffer/image/sampler SHARPs.
    // To be conservative, a changed binding-validity bitset always misses.
    // Fetch shaders and tessellation programs use the original full path.
    [[nodiscard]] bool MatchesCurrentNonFetchGraphics(const Info& current,
        const RuntimeInfo& runtime, const Backend::Bindings& current_start) const {
        if (!Valid() || current.has_fetch_shader || fetch_shader_data ||
            !vs_attribs.empty() || current.sw_stage != info->sw_stage ||
            (current.sw_stage != SwStage::Vertex && current.sw_stage != SwStage::Fragment) ||
            runtime_info != runtime ||
            current.buffers.size() != buffers.size() ||
            current.images.size() != images.size() ||
            current.fmasks.size() != fmasks.size() ||
            current.samplers.size() != samplers.size()) {
            return false;
        }
        std::bitset<MaxStageResources> fresh_bits{};
        u32 binding = 0;
        for (u32 i = 0; i < current.buffers.size(); ++i, ++binding) {
            const auto& desc = current.buffers[i];
            const auto sharp = desc.GetSharp(current);
            if (!sharp) continue;
            fresh_bits.set(binding);
            BufferSpecialization fresh{};
            fresh.stride = sharp.GetStride();
            fresh.is_formatted = desc.is_formatted;
            fresh.swizzle_enable = sharp.swizzle_enable;
            if (fresh.is_formatted) {
                fresh.data_format = static_cast<u32>(sharp.GetDataFmt());
                fresh.num_format = static_cast<u32>(sharp.GetNumberFmt());
                fresh.dst_select = sharp.DstSelect();
                fresh.num_conversion = sharp.GetNumberConversion();
            }
            if (fresh.swizzle_enable) {
                fresh.index_stride = sharp.index_stride;
                fresh.element_size = sharp.element_size;
            }
            if (fresh != buffers[i]) return false;
        }
        for (u32 i = 0; i < current.images.size(); ++i, ++binding) {
            const auto& desc = current.images[i];
            // Dynamic mip expansion uses another fresh guest fetch and must
            // retain full specialization (and its original refresh timing).
            if (desc.mip_fallback_mode == MipStorageFallbackMode::DynamicIndex) return false;
            const auto sharp = desc.GetSharp(current);
            if (!sharp) continue;
            fresh_bits.set(binding);
            ImageSpecialization fresh{};
            fresh.type = sharp.GetViewType(desc.is_array);
            fresh.is_integer = AmdGpu::IsInteger(sharp.GetNumberFmt());
            fresh.is_storage = desc.is_written;
            fresh.is_cube = sharp.IsCube();
            if (fresh.is_storage) {
                fresh.dst_select = sharp.DstSelect();
            } else {
                fresh.is_srgb = sharp.GetNumberFmt() == AmdGpu::NumberFormat::Srgb;
            }
            fresh.num_conversion = sharp.GetNumberConversion();
            fresh.num_bindings = 1; // only non-dynamic mip fallback reaches this path
            if (fresh != images[i]) return false;
        }
        for (u32 i = 0; i < current.fmasks.size(); ++i, ++binding) {
            const auto sharp = current.fmasks[i].GetSharp(current);
            FMaskSpecialization fresh{};
            if (sharp) {
                fresh_bits.set(binding);
                fresh.width = sharp.width;
                fresh.height = sharp.height;
            }
            if (fresh != fmasks[i]) return false;
        }
        // Same two-phase equality policy as operator==. Tightening the
        // binding mask comparison prevents a false hit on an invalid buffer
        // while never omitting a valid old-path module.
        if (fresh_bits != bitset) return false;
        if (fresh_bits.none()) return true;
        if (start != current_start) return false;
        for (u32 i = 0; i < current.samplers.size(); ++i) {
            const auto sharp = current.samplers[i].GetSharp(current);
            SamplerSpecialization fresh{};
            if (sharp) {
                fresh.force_unnormalized = sharp.force_unnormalized;
                fresh.force_degamma = sharp.force_degamma;
            }
            if (fresh != samplers[i]) return false;
        }
        return true;
    }

    void ForEachSharp(auto& spec_list, auto& desc_list, auto&& func) {
        for (const auto& desc : desc_list) {
            auto& spec = spec_list.emplace_back();
            const auto sharp = desc.GetSharp(*info);
            if (!sharp) {
                continue;
            }
            func(spec, desc, sharp);
        }
    }

    void ForEachSharp(u32& binding, auto& spec_list, auto& desc_list, auto&& func) {
        for (const auto& desc : desc_list) {
            auto& spec = spec_list.emplace_back();
            const auto sharp = desc.GetSharp(*info);
            if (!sharp) {
                binding++;
                continue;
            }
            bitset.set(binding++);
            func(spec, desc, sharp);
        }
    }

    [[nodiscard]] bool Valid() const {
        return info != nullptr;
    }

    bool operator==(const StageSpecialization& other) const {
        if (!Valid()) {
            return false;
        }

        if (vs_attribs != other.vs_attribs) {
            return false;
        }

        if (runtime_info != other.runtime_info) {
            return false;
        }

        if (fetch_shader_data != other.fetch_shader_data) {
            return false;
        }

        if (fmasks != other.fmasks) {
            return false;
        }

        // For VS which only generates geometry and doesn't have any inputs, its start
        // bindings still may change as they depend on previously processed FS. The check below
        // handles this case and prevents generation of redundant permutations. This is also safe
        // for other types of shaders with no bindings.
        if (bitset.none() && other.bitset.none()) {
            return true;
        }

        if (start != other.start) {
            return false;
        }

        u32 binding{};
        for (u32 i = 0; i < buffers.size(); i++) {
            if (other.bitset[binding++] && buffers[i] != other.buffers[i]) {
                return false;
            }
        }
        for (u32 i = 0; i < images.size(); i++) {
            if (other.bitset[binding++] && images[i] != other.images[i]) {
                return false;
            }
        }

        for (u32 i = 0; i < samplers.size(); i++) {
            if (samplers[i] != other.samplers[i]) {
                return false;
            }
        }
        return true;
    }

    void Serialize(Serialization::Archive& ar) const;
    bool Deserialize(Serialization::Archive& ar);
};

} // namespace Shader
