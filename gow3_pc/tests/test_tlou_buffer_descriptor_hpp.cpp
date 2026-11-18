// SPDX-License-Identifier: GPL-2.0-or-later
#include "video_core/renderer_vulkan/vk_common.h"
#include "video_core/renderer_vulkan/tlou_buffer_descriptor_template.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE
static const void* received;
static VKAPI_ATTR void VKAPI_CALL Capture(VkCommandBuffer, VkDescriptorUpdateTemplate,
                                          VkPipelineLayout, uint32_t, const void* data) {
    received = data;
}
int main() {
    VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdPushDescriptorSetWithTemplateKHR = Capture;
    std::array<vk::DescriptorBufferInfo,16> payload{};
    const vk::CommandBuffer command{reinterpret_cast<VkCommandBuffer>(1)};
    const vk::DescriptorUpdateTemplate update{};
    const vk::PipelineLayout layout{};
    // Reproduce the exact original production call. Hpp forwards an address
    // of its pointer argument, not the beginning of the descriptor array.
    command.pushDescriptorSetWithTemplateKHR(update,layout,0,payload.data());
    assert(received != payload.data());
    std::puts("Original typed-pointer Vulkan-Hpp overload bug reproduced PASS");
    for(unsigned i=0;i<100000;++i) {
        payload[i%payload.size()].offset = i*256ull;
        payload[i%payload.size()].range = i%4096+1ull;
        Vulkan::Detail::PushBufferDescriptorTemplate(
            command,update,layout,std::span<const vk::DescriptorBufferInfo>{payload});
        assert(received == payload.data());
        std::array<vk::DescriptorBufferInfo,16> delivered{};
        std::memcpy(delivered.data(),received,sizeof(delivered));
        assert(delivered == payload);
    }
    std::puts("Production wrapper forwards exact descriptor address and bytes PASS");
}
