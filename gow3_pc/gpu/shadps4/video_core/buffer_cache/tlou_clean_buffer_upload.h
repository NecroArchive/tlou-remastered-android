// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace VideoCore::Detail {
// The original read-only ForEachUploadRange already makes this exact
// lock-free dirty-bit check and immediately returns for a clean region.
// Avoid constructing copy lists and invoking an empty upload callback when
// *all* mapped CPU pages are clean. GPU writes and texel-image alias paths
// must always run the original synchronization code.
template <class CpuDirtyProbe>
inline bool CanSkipCleanBufferUpload(bool written, bool texel,
                                     bool lockfree_enabled,
                                     CpuDirtyProbe&& cpu_dirty) {
    return !written && !texel && lockfree_enabled && !cpu_dirty();
}
} // namespace VideoCore::Detail
