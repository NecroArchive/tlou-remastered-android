// SPDX-License-Identifier: GPL-2.0-or-later
#include <array>
#include <cassert>
#include <cstdio>
#include <span>
#include "video_core/renderer_vulkan/tlou_buffer_descriptor_template.h"

struct BufferInfo { unsigned long long buffer, offset, range; };
struct Write {
    const void* pNext{};
    unsigned dstBinding{}, dstArrayElement{}, descriptorCount{}, descriptorType{};
    const BufferInfo* pBufferInfo{};
};
int main() {
    std::array<BufferInfo, 8> buffers{};
    std::array<Write, 8> writes{};
    const auto matches = [&](size_t n = 8) {
        return Vulkan::Detail::MatchesBufferDescriptorTemplate(
            std::span<const Write>{writes}, std::span<const BufferInfo>{buffers}, n, 7u);
    };
    for (unsigned i = 0; i < writes.size(); ++i) writes[i] = {nullptr,i,0,1,7,&buffers[i]};
    assert(matches());
    assert(!matches(2)); assert(!matches(7)); assert(!matches(9));
    const auto saved = writes[3];
    writes[3].pNext = &writes; assert(!matches()); writes[3] = saved;
    writes[3].dstBinding = 4; assert(!matches()); writes[3] = saved;
    writes[3].dstArrayElement = 1; assert(!matches()); writes[3] = saved;
    writes[3].descriptorCount = 2; assert(!matches()); writes[3] = saved;
    writes[3].descriptorType = 6; assert(!matches()); writes[3] = saved;
    writes[3].pBufferInfo = nullptr; assert(!matches()); writes[3] = saved;
    writes[3].pBufferInfo = &buffers[4]; assert(!matches()); writes[3] = saved;
    // Dynamic contents change every dispatch; the guard must accept all of
    // them without caching a previous buffer handle, address or range.
    for (unsigned i = 0; i < 100000; ++i) {
        buffers[i%8] = {i, i*256ull, i%4096+1ull};
        assert(matches());
    }
    std::puts("Buffer-template dynamic payload and all fallback guards PASS");
}
