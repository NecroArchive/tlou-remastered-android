// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cstdlib>

#include "video_core/renderer_vulkan/vk_gpu_profiler.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"

namespace Vulkan {

void GpuProfiler::Init(const Instance& instance, Scheduler& scheduler) {
    const char* env = std::getenv("GOW3_GPU_PROFILE");
    if (!env || env[0] != '1' || instance_ptr) {
        return;
    }
    const auto physical = instance.GetPhysicalDevice();
    const auto families = physical.getQueueFamilyProperties();
    const auto feature_chain = physical.getFeatures2<vk::PhysicalDeviceFeatures2,
                                                     vk::PhysicalDeviceVulkan12Features>();
    if (instance.GetGraphicsQueueFamilyIndex() >= families.size() ||
        families[instance.GetGraphicsQueueFamilyIndex()].timestampValidBits == 0 ||
        !feature_chain.get<vk::PhysicalDeviceVulkan12Features>().hostQueryReset ||
        physical.getProperties().limits.timestampPeriod <= 0.0f) {
        std::printf("GPU profile: unavailable (timestamps/host query reset unsupported)\n");
        return;
    }
    instance_ptr = new GpuProfiler(instance, scheduler);
}

GpuProfiler::GpuProfiler(const Instance& instance, Scheduler& scheduler_)
    : device{instance.GetDevice()}, scheduler{scheduler_} {
    const vk::QueryPoolCreateInfo info = {
        .queryType = vk::QueryType::eTimestamp,
        .queryCount = NumSlices * SliceQueries,
    };
    pool = Check<"create timestamp query pool">(device.createQueryPoolUnique(info));
    device.resetQueryPool(*pool, 0, NumSlices * SliceQueries);
    period_ns = instance.GetPhysicalDevice().getProperties().limits.timestampPeriod;
    const auto valid_bits = instance.GetPhysicalDevice().getQueueFamilyProperties()
                                [instance.GetGraphicsQueueFamilyIndex()].timestampValidBits;
    timestamp_mask = valid_bits >= 64 ? ~u64{0} : (u64{1} << valid_bits) - 1;
    for (auto& k : keys) {
        k.reserve(SliceQueries);
    }
    const char* value = std::getenv("TLOU_GPU_PROFILE_STRIDE");
    if (value) {
        char* end = nullptr;
        const unsigned long parsed = std::strtoul(value, &end, 10);
        if (end != value && *end == '\0' && parsed >= 1 && parsed <= 128) {
            sample_stride = static_cast<u32>(parsed);
        }
    }
    sample_active = sample_stride == 1;
    std::printf("GPU profile: on (timestamp period %.2f ns, frame stride %u)\n",
                period_ns, sample_stride);
}

void GpuProfiler::WriteTimestamp(u64 key) {
    if (used[slice] + 1 >= SliceQueries) {
        truncated[slice] = true;
        return; // frame's slice is full; mark the GPU label breakdown incomplete
    }
    // Outside render passes: radv_CmdWriteTimestamp2 crashed inside some. Marks sit where a
    // pass, dispatch or submission ends anyway.
    scheduler.EndRendering();
    const u32 query = slice * SliceQueries + used[slice]++;
    keys[slice].push_back(key);
    current = key;
    scheduler.Record([pool = *pool, query](vk::CommandBuffer cmdbuf) {
        cmdbuf.writeTimestamp2(vk::PipelineStageFlagBits2::eAllCommands, pool, query);
    });
}

void GpuProfiler::BeginFrame() {
    // Close the frame: one more timestamp without a label.
    if (used[slice] > 0 && used[slice] < SliceQueries) {
        scheduler.EndRendering();
        const u32 query = slice * SliceQueries + used[slice]++;
        scheduler.Record([pool = *pool, query](vk::CommandBuffer cmdbuf) {
            cmdbuf.writeTimestamp2(vk::PipelineStageFlagBits2::eAllCommands, pool, query);
        });
        pending[slice] = true;
    }
    slice = (slice + 1) % NumSlices;
    // The oldest slice: its frame was submitted four frames ago.
    if (pending[slice]) {
        Collect(slice);
    }
    if (used[slice]) {
        device.resetQueryPool(*pool, slice * SliceQueries, used[slice]);
    }
    used[slice] = 0;
    keys[slice].clear();
    pending[slice] = false;
    truncated[slice] = false;
    sample_active = ((frame_counter++ % sample_stride) == 0);
    Print();
}

void GpuProfiler::Collect(u32 which) {
    const u32 count = used[which];
    std::vector<u64> stamps(count);
    // Four frames on this is complete unless the GPU lags that far: then wait for it.
    const auto result = device.getQueryPoolResults(
        *pool, which * SliceQueries, count, count * sizeof(u64), stamps.data(), sizeof(u64),
        vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWait);
    if (result != vk::Result::eSuccess) {
        return;
    }
    for (u32 i = 0; i + 1 < count; ++i) {
        const double ms = double((stamps[i + 1] - stamps[i]) & timestamp_mask) * period_ns / 1e6;
        auto& total = totals[keys[which][i]];
        total.ms += ms;
        ++total.segments;
    }
    ++frames;
    if (truncated[which]) ++truncated_frames;
}

void GpuProfiler::Print() {
    const auto now = std::chrono::steady_clock::now();
    if (now - window < std::chrono::seconds(5) || frames == 0) {
        return;
    }
    window = now;
    std::vector<std::pair<u64, Total>> sorted(totals.begin(), totals.end());
    std::ranges::sort(sorted, [](const auto& a, const auto& b) { return a.second.ms > b.second.ms; });
    double sum = 0;
    for (const auto& [key, total] : sorted) {
        sum += total.ms;
    }
    // Timestamp segments measure GPU queue timeline, not GPU utilization.
    // Only frames actually sampled are included in this average.
    std::printf("GPU profile: %.2f ms/sampled-frame over %llu frames (stride %u, "
                "incomplete-label-frames %llu), %zu labels\n", sum / frames,
                static_cast<unsigned long long>(frames), sample_stride,
                static_cast<unsigned long long>(truncated_frames), sorted.size());
    for (size_t i = 0; i < std::min<size_t>(sorted.size(), 30); ++i) {
        const auto& [key, total] = sorted[i];
        std::printf("  %6.3f ms/frame %5.1f/frame  %s\n", total.ms / frames,
                    double(total.segments) / frames, described[key].c_str());
    }
    totals.clear();
    frames = 0;
    truncated_frames = 0;
}

} // namespace Vulkan
