#!/usr/bin/env python3
"""Exercise the actual generated AOT fault recorder without game assets."""
from __future__ import annotations

import importlib.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

module = importlib.util.spec_from_file_location("tlou_port_runtime", Path(__file__).with_name("port_runtime.py"))
port = importlib.util.module_from_spec(module)
module.loader.exec_module(port)

HOST = r"""
#include <atomic>
#include <cstdint>
#include <csignal>
#include <cstddef>
#include <sys/types.h>
#include <sys/syscall.h>
#include <unistd.h>
struct GuestState { uint64_t rip=0, gregs[16]{}; };
struct Thread { GuestState* s; };
static thread_local Thread* self=nullptr;
static struct { uintptr_t so_lo=0x100000, so_hi=0x200000; } E;
enum { RSP=4 };
namespace bbnative { uintptr_t HostPc(const void*) { return 0x110000; } }
@TRACE@
int main() {
    GuestState g; g.rip=0xb5a041; g.gregs[RSP]=0x1200000000ULL;
    Thread t{&g}; self=&t;
    siginfo_t info{}; info.si_addr=reinterpret_cast<void*>(uintptr_t(0xdeadbeef));
    info.si_code=SEGV_MAPERR;
    TlouAotProbeFault(SIGSEGV,&info,nullptr);
    for (int n=0; n<62; ++n) TlouAotProbeFault(SIGSEGV,&info,nullptr);
}
"""

class FaultProbeTests(unittest.TestCase):
    def test_recorder_compiles_without_game_and_caps_output(self):
        cxx=shutil.which("clang++") or shutil.which("g++")
        if not cxx:
            self.skipTest("No C++20 compiler")
        self.assertIn("TLOU AOT fault entry",port.FATAL_SIGNAL_PROBE)
        self.assertNotIn("sigaction(",port.FATAL_SIGNAL_PROBE)
        self.assertNotIn("fprintf",port.FATAL_SIGNAL_PROBE)
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            cpp=root/"probe.cpp"; elf=root/"probe"
            cpp.write_text(HOST.replace("@TRACE@",port.FATAL_SIGNAL_PROBE),encoding="utf8")
            subprocess.run([cxx,"-std=c++20","-O2","-Wall","-Wextra","-Werror",str(cpp),"-o",str(elf)],
                           check=True,capture_output=True,text=True)
            r=subprocess.run([str(elf)],capture_output=True,text=True,check=True)
            self.assertIn("TLOU AOT fault entry",r.stderr)
            self.assertIn("0x0000000000b5a041",r.stderr)
            self.assertIn("0x00000000deadbeef",r.stderr)
            self.assertEqual(len(r.stderr.splitlines()),48)
            self.assertIn("0x0000000000000030",r.stderr)

if __name__=="__main__":
    unittest.main()
