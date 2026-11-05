// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include "gow3_copy.h"

// Does NOT allocate a fixed 16-slot array per batch. Instead it owns a
// pointer to contiguous Gow3Copy::Item records in the CURRENT unpublished
// Vulkan RecordChunk. On another command or a chunk flush no more records
// are appended. Items remain live until this command executes.
//
// host_copies_issued still increments for every individual copy. The final
// release-store publishes last_sequence only after ALL the batch's copies
// have run, which is conservative for waiters on intermediate sequences.
namespace Vulkan::Detail {
class HostCopyBatch {
public:
    HostCopyBatch(std::atomic<std::uint64_t>& done, Gow3Copy::Item* first,
                  std::uint64_t sequence) noexcept
        : copies{reinterpret_cast<const std::byte*>(first)}, completed{&done},
          last_sequence{sequence}, used{1} {}

    [[nodiscard]] bool Append(Gow3Copy::Item* next, std::uint64_t sequence) noexcept {
        if (reinterpret_cast<const std::byte*>(next) !=
                copies + used * sizeof(Gow3Copy::Item) || sequence <= last_sequence) {
            return false;
        }
        ++used;
        last_sequence = sequence;
        return true;
    }

    void Execute() const {
        for (std::size_t i = 0; i != used; ++i) {
            const auto& item = *std::launder(reinterpret_cast<const Gow3Copy::Item*>(
                copies + i * sizeof(Gow3Copy::Item)));
            item.run(item);
        }
        completed->store(last_sequence, std::memory_order_release);
    }

    [[nodiscard]] std::size_t Size() const noexcept { return used; }
    [[nodiscard]] std::uint64_t LastSequence() const noexcept { return last_sequence; }

private:
    const std::byte* copies;
    std::atomic<std::uint64_t>* completed;
    std::uint64_t last_sequence;
    std::size_t used;
};
} // namespace Vulkan::Detail
