// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstddef>

namespace Vulkan::Detail {
// CollectRingBindings appends at most one binding per buffer, in increasing index order.
// BindBuffers consumes increasing buffer indices. The cursor lives for that call only;
// it neither caches guest data nor survives the packet's resource ownership.
template<class Binding>
class RingBindingCursor {
public:
    RingBindingCursor(const Binding* bindings, std::size_t count) : next(bindings), left(count) {}
    const Binding* Take(std::size_t index) {
        while (left && next->index < index) {
            ++next;
            --left;
        }
        if (left && next->index == index) {
            const Binding* found = next++;
            --left;
            return found;
        }
        return nullptr;
    }
private:
    const Binding* next;
    std::size_t left;
};

} // namespace Vulkan::Detail
