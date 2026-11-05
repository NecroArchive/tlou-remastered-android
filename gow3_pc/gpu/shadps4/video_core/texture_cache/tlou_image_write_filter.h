// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <utility>
#include <atomic>
#include <cstdint>

// Monotonic, two-hash filter of image guest BASE addresses.
// False positives only: a hit still performs the ordinary locked image lookup.
// A negative is safe because the invalidation callback only affects images
// whose info.guest_address is EXACTLY the written buffer's starting address.
// Bits are never cleared on image eviction/unmap/GC, avoiding an epoch race.
// Release/acquire ensures a completed registration is visible to a later check.
namespace VideoCore::Detail {
class ImageBaseWriteFilter {
public:
    static constexpr std::size_t WordCount = 2048; // 128 Ki bits, 16 KiB

    void Register(std::uint64_t address) noexcept {
        const auto [first, second] = Indices(address);
        bitmap[first >> 6].fetch_or(std::uint64_t{1} << (first & 63),
                                    std::memory_order_release);
        bitmap[second >> 6].fetch_or(std::uint64_t{1} << (second & 63),
                                     std::memory_order_release);
    }

    [[nodiscard]] bool MayContain(std::uint64_t address) const noexcept {
        const auto [first, second] = Indices(address);
        if (!(bitmap[first >> 6].load(std::memory_order_acquire) &
              (std::uint64_t{1} << (first & 63)))) {
            return false;
        }
        return (bitmap[second >> 6].load(std::memory_order_acquire) &
                (std::uint64_t{1} << (second & 63))) != 0;
    }

private:
    static constexpr std::uint64_t Mix(std::uint64_t value) noexcept {
        value ^= value >> 33;
        value *= 0xff51afd7ed558ccdULL;
        value ^= value >> 33;
        value *= 0xc4ceb9fe1a85ec53ULL;
        return value ^ (value >> 33);
    }

    static constexpr std::pair<std::size_t, std::size_t>
    Indices(std::uint64_t address) noexcept {
        constexpr auto mask = WordCount * 64 - 1;
        // Full 64-bit guest base: 16-byte alignment is common, not required.
        const auto hash = Mix(address);
        return {static_cast<std::size_t>(hash) & mask,
                static_cast<std::size_t>(Mix(address ^ 0x9e3779b97f4a7c15ULL)) & mask};
    }

    std::array<std::atomic<std::uint64_t>, WordCount> bitmap{};
};
} // namespace VideoCore::Detail
