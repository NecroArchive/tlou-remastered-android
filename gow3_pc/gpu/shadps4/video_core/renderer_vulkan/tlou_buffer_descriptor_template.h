// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstddef>
#include <span>

namespace Vulkan::Detail {
// Vulkan-Hpp also offers a const DataType& overload. A typed pointer selects
// that overload and forwards &pointer, not the descriptor payload. Force the
// raw void-pointer API; callers and tests share this exact wrapper.
template <class CommandBuffer, class Update, class Layout, class BufferInfo>
void PushBufferDescriptorTemplate(CommandBuffer command, Update update, Layout layout,
                                  std::span<const BufferInfo> buffers) {
    command.pushDescriptorSetWithTemplateKHR(
        update, layout, 0, static_cast<const void*>(buffers.data()));
}

// Only the immutable descriptor layout is reused. Every invocation still
// snapshots the current buffer handles, offsets and ranges for its dispatch.
// Keep unusual writes (arrays, extension chains, noncanonical pointers) on
// the ordinary push-descriptor path.
template <class Write, class BufferInfo, class DescriptorType>
bool MatchesBufferDescriptorTemplate(std::span<const Write> writes,
                                     std::span<const BufferInfo> buffers,
                                     std::size_t count, DescriptorType storage_type) {
    if (count < 4 || writes.size() != count || buffers.size() != count) {
        return false;
    }
    for (std::size_t i = 0; i < count; ++i) {
        const auto& write = writes[i];
        if (write.pNext || write.dstBinding != i || write.dstArrayElement != 0 ||
            write.descriptorCount != 1 || write.descriptorType != storage_type ||
            write.pBufferInfo != buffers.data() + i) {
            return false;
        }
    }
    return true;
}
} // namespace Vulkan::Detail
