#!/usr/bin/env python3
"""Compile and execute the *actual* TLOU AOT HostCall fiber-return snippet.

Catches regressions where a fiber prepared on the main host thread replaces
the TLS base belonging to the executing PS4 worker thread. No game files.
"""
from __future__ import annotations

import importlib.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("tlou_aot_port", Path(__file__).with_name("port_runtime.py"))
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

CPP = r"""
#include <cstdint>
#include <cstdio>
#include <sys/syscall.h>
#include <unistd.h>

enum { RSP = 4 };
struct GuestState {
    uint64_t gregs[16]{}, rip{}, fs_cached{}, gs_cached{};
    uint64_t private_gdt[8]{};
    uint64_t* segment_arrays[2]{};
};
struct GuestFiberCpuState { GuestState state; };
thread_local GuestFiberCpuState* tlou_pending_fiber = nullptr;
thread_local void (*tlou_pending_release)(void*) = nullptr;
thread_local void* tlou_pending_previous = nullptr;
uint64_t ReadGuest(uint64_t) { return 0; }

void apply_fiber_return(GuestState* s, uint64_t sp, uint64_t ret) {
@HOST_RETURN@
}

int release_calls = 0;
void release(void* p) {
    if (p == reinterpret_cast<void*>(uintptr_t(0x1234))) ++release_calls;
}

int main() {
    GuestState worker{};
    worker.fs_cached = 0x8010700b0ULL;
    worker.gs_cached = 0x8010700b0ULL;
    worker.gregs[0] = 12;
    worker.gregs[RSP] = 0x1000;
    worker.rip = 0x400010;
    GuestFiberCpuState fiber{};
    fiber.state.fs_cached = 0x8003ec0b0ULL;  // main thread that created it
    fiber.state.gs_cached = 0x8003ec0b0ULL;
    fiber.state.gregs[RSP] = 0x12011eb000ULL;
    fiber.state.gregs[0] = 43;
    fiber.state.rip = 0x106a050ULL;
    tlou_pending_fiber = &fiber;
    tlou_pending_release = release;
    tlou_pending_previous = reinterpret_cast<void*>(uintptr_t(0x1234));
    apply_fiber_return(&worker, 0x1000, 0x400018);
    if (worker.fs_cached != 0x8010700b0ULL ||
        worker.gs_cached != 0x8010700b0ULL) return 1;
    if (worker.rip != 0x106a050ULL ||
        worker.gregs[RSP] != 0x12011eb000ULL ||
        worker.gregs[0] != 43) return 2;
    if (worker.segment_arrays[0] != worker.private_gdt ||
        worker.segment_arrays[1] != worker.private_gdt) return 3;
    if (tlou_pending_fiber || tlou_pending_release ||
        tlou_pending_previous || release_calls != 1) return 4;

    // A normal guest-to-host return must still advance its stack and RIP.
    apply_fiber_return(&worker, 0x12011eb000ULL, 0x106a080ULL);
    if (worker.gregs[RSP] != 0x12011eb008ULL ||
        worker.rip != 0x106a080ULL ||
        worker.fs_cached != 0x8010700b0ULL ||
        worker.gs_cached != 0x8010700b0ULL) return 5;
    return 0;
}
"""


class FiberTlsTests(unittest.TestCase):
    def test_current_worker_tls_survives_fiber_restore(self):
        compiler = shutil.which("clang++") or shutil.which("g++")
        if not compiler:
            self.skipTest("No host C++ compiler available")
        self.assertIn("s->fs_cached = worker_fs;", mod.HOST_RETURN)
        self.assertIn("s->gs_cached = worker_gs;", mod.HOST_RETURN)
        self.assertNotIn("s->gs_cached = saved_fiber_gs;", mod.HOST_RETURN)
        program = CPP.replace("@HOST_RETURN@", mod.HOST_RETURN)
        with tempfile.TemporaryDirectory() as td:
            cpp = Path(td) / "fiber_tls.cpp"
            binary = Path(td) / "fiber_tls"
            cpp.write_text(program)
            subprocess.run([compiler, "-std=c++20", "-O2", "-Wall", "-Wextra",
                            "-Werror", str(cpp), "-o", str(binary)],
                           check=True, capture_output=True, text=True)
            subprocess.run([str(binary)], check=True, capture_output=True, text=True)


if __name__ == "__main__":
    unittest.main()
