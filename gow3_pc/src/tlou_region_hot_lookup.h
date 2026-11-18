/* SPDX-License-Identifier: GPL-2.0-or-later
 * Hot guest VMA lookup for TLOU's Stage-A constant snapshot and Stage-B
 * buffer upload paths. All positive entries are validated against the
 * runtime VM table generation AND exact byte bounds before being used.
 * The cache is an optimization only; collisions and remaps fall back to
 * the existing 8-entry cache and locked VMA walk. */
#ifndef TLOU_REGION_HOT_LOOKUP_H
#define TLOU_REGION_HOT_LOOKUP_H
#include <stdint.h>
#include <stddef.h>
enum { TLOU_HOT_REGION_SLOTS = 256 };
static inline size_t tlou_hot_region_slot(uintptr_t address) {
    /* Mix 4-KiB guest pages. Zero-collision regions remain hot even after
     * dozens of unrelated SRT descriptors between repeated bindings. */
    return (size_t)(((uint64_t)(address >> 12) * UINT64_C(0x9e3779b97f4a7c15)) >>
                    (64 - 8));
}
static inline int tlou_hot_region_contains(uint64_t seen_generation,
                                           uint64_t current_generation,
                                           uintptr_t start, uintptr_t end,
                                           uintptr_t address) {
    return !(current_generation & 1) && seen_generation == current_generation &&
           end != 0 && start <= address && address < end;
}
#endif
