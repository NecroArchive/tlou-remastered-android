// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include "gow3_copy.h"

// A contiguous sequence of guest-to-host copies within ONE deferred Vulkan
// command chunk. No Vulkan command, data reservation, flush, guest-visible
// fence or scheduler operation may be interleaved in a batch.
//
// host_copies_issued still increments for each copy. Publishing last_sequence
// *after all copies* is conservative: waiters for intermediate sequences wait
// longer, never see guest bytes early. No guest-memory snapshots are reused.
namespace Vulkan::Detail {
template<std::size_t Capacity = 16>
class HostCopyBatch {
public:
    HostCopyBatch(std::atomic<std::uint64_t>& done, const Gow3Copy::Item& first,
                  std::uint64_t sequence) noexcept
        : completed{&done}, last_sequence{sequence}, used{1} {
        copies[0] = first;
    }

    [[nodiscard]] bool Append(const Gow3Copy::Item& item, std::uint64_t sequence) noexcept {
        if (used == Capacity) {
            return false;
        }
        // Sequences are issued in submission order on the recording caller.
        if (sequence <= last_sequence) {
            return false;
        }
        copies[used++] = item;
        last_sequence = sequence;
        return true;
    }

    void Execute() const {
        for (std::size_t i = 0; i != used; ++i) {
            const auto& item = copies[i];
            item.run(item);
        }
        completed->store(last_sequence, std::memory_order_release);
    }

    [[nodiscard]] std::size_t Size() const noexcept { return used; }
    [[nodiscard]] std::uint64_t LastSequence() const noexcept { return last_sequence; }

private:
    std::array<Gow3Copy::Item, Capacity> copies{};
    std::atomic<std::uint64_t>* completed;
    std::uint64_t last_sequence;
    std::size_t used;
};
} // namespace Vulkan::Detail
