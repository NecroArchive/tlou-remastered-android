// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

namespace Vulkan::Detail {

// Disabled debug markers have no Vulkan commands or GPU side effects.
// Do not serialize Stage A and Stage B until an actual marker will be emitted.
[[nodiscard]] constexpr bool MarkerNeedsRecording(bool from_guest,
                                                    bool guest_enabled,
                                                    bool host_enabled) noexcept {
    return from_guest ? guest_enabled : host_enabled;
}
