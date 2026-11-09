/* SPDX-License-Identifier: GPL-2.0-or-later
 * One small guest upload that straddles exactly two adjacent, backed VMAs.
 * This is a pure helper: the caller must obtain snapshots from the per-thread
 * generation-checked VMA cache; invalid/incomplete spans fall back to the
 * original locked sparse walk. Never copy via the guest protected view.
 */
#ifndef TLOU_BACKING_SPAN_COPY_H
#define TLOU_BACKING_SPAN_COPY_H
#include <stdint.h>
#include <string.h>

typedef struct {
    uintptr_t start, end;
    uint64_t generation;
    const unsigned char *physical_start;
    int mapped;
} TlouBackingSpan;

static inline int tlou_copy_two_backing(uintptr_t address, void *dest, uint64_t size,
                                        TlouBackingSpan a, TlouBackingSpan b) {
    if (!size || !a.mapped || !b.mapped || !a.physical_start || !b.physical_start ||
        (a.generation & 1u) || a.generation != b.generation ||
        address < a.start || address >= a.end || b.start != a.end ||
        b.end <= b.start) return 0;
    const uint64_t first = a.end - address;
    if (first >= size || size - first > b.end - b.start) return 0;
    memcpy(dest, a.physical_start + (address - a.start), first);
    memcpy((unsigned char*)dest + first, b.physical_start, size - first);
    return 1;
}
#endif
