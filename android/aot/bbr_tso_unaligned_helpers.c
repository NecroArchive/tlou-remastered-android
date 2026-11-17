// SPDX-License-Identifier: GPL-3.0-or-later
// TLOU AOT TSO v5: out-of-line *unaligned scalar only* slow path.
// This is linked into helpers-aarch64.o (no libc dependency beyond builtins).
// Naturally aligned scalar accesses still execute LLVM Acquire/Release LDAR/STLR.
#include <stdint.h>

// Fences must not move across the guest memory access or be optimized away.
// LLVM's original slow path emitted full sequentially consistent fences.
__attribute__((noinline, cold, visibility("default")))
uint64_t bbr_tso_unaligned_load(uint64_t address, uint32_t size) {
    const void *p = (const void *)(uintptr_t)address;
    uint64_t value = 0;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    switch (size) {
        case 2: __builtin_memcpy(&value, p, 2); break;
        case 4: __builtin_memcpy(&value, p, 4); break;
        case 8: __builtin_memcpy(&value, p, 8); break;
        default: __builtin_trap();
    }
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    return value;
}

__attribute__((noinline, cold, visibility("default")))
void bbr_tso_unaligned_store(uint64_t address, uint64_t value, uint32_t size) {
    void *p = (void *)(uintptr_t)address;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    switch (size) {
        case 2: __builtin_memcpy(p, &value, 2); break;
        case 4: __builtin_memcpy(p, &value, 4); break;
        case 8: __builtin_memcpy(p, &value, 8); break;
        default: __builtin_trap();
    }
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}
