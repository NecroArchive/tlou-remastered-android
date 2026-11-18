#!/usr/bin/env python3
"""Build and run the real native AOT signal-altstack integration on host Linux.

Tests that we retain the installed signal handler and deliver a SIGSEGV on
the per-thread alternate stack, even on a short-lived worker thread.
"""
from __future__ import annotations

import importlib.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

spec=importlib.util.spec_from_file_location("tlou_runtime_port",Path(__file__).with_name("port_runtime.py"))
port=importlib.util.module_from_spec(spec)
spec.loader.exec_module(port)

CPP=r"""
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <csignal>
#include <mutex>
#include <thread>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

// Count actual signal-stack syscalls performed by the port implementation.
// The signal handler and its original Android recovery remain unchanged.
static std::atomic<unsigned> tracked_calls{0};
static int traced_sigaltstack(const stack_t* ss, stack_t* old) {
    tracked_calls.fetch_add(1, std::memory_order_relaxed);
    return ::sigaltstack(ss, old);
}
#define sigaltstack traced_sigaltstack
@IMPL@
#undef sigaltstack

static thread_local int handler_calls=0;
static thread_local bool on_alternate_stack=false;
void original_fault(int, siginfo_t*, void*) {
    char location=0;
    const uintptr_t local=(uintptr_t)&location;
    const uintptr_t base=(uintptr_t)tlou_signal_stack;
    on_alternate_stack = base && local >= base &&
                          local < base + TlouSignalStackSize;
    ++handler_calls;
}

int check() {
    TlouEnsureSignalStack();
    const unsigned setup_queries = tracked_calls.load(std::memory_order_relaxed);
    for (int i=0; i<100000; ++i) TlouEnsureSignalStack();
    if (tracked_calls.load(std::memory_order_relaxed) != setup_queries) return 8;
    if (!tlou_signal_stack_ready) return 9;
    struct sigaction actual{};
    if(sigaction(SIGSEGV,nullptr,&actual)!=0) return 1;
    if(!(actual.sa_flags & SA_ONSTACK)) return 2;
    if(actual.sa_sigaction != original_fault) return 3;
    stack_t ss{};
    if(sigaltstack(nullptr,&ss)!=0 || (ss.ss_flags & SS_DISABLE) ||
       ss.ss_size < 65536) return 4;
    if(raise(SIGSEGV)!=0) return 5;
    if(handler_calls!=1 || !on_alternate_stack) return 6;
    TlouReleaseSignalStack();
    if(sigaltstack(nullptr,&ss)!=0 || !(ss.ss_flags & SS_DISABLE))return 7;
    // A guest-thread reentry after explicit cleanup must rearm the altstack.
    TlouEnsureSignalStack();
    if (!tlou_signal_stack_ready || sigaltstack(nullptr,&ss)!=0 ||
        (ss.ss_flags & SS_DISABLE)) return 10;
    TlouReleaseSignalStack();
    return 0;
}

int main() {
    struct sigaction initial{};
    initial.sa_sigaction=original_fault;
    initial.sa_flags=SA_SIGINFO;
    sigemptyset(&initial.sa_mask);
    if(sigaction(SIGSEGV,&initial,nullptr)!=0)return 20;
    int first=check();
    if(first)return 30+first;
    int other=-1;
    std::thread worker([&]{other=check();});
    worker.join();
    if(other)return 40+other;
    return 0;
}
"""

class AltSignalStackTests(unittest.TestCase):
    def test_signal_handler_runs_on_altstack_on_main_and_worker(self):
        compiler=shutil.which("clang++") or shutil.which("g++")
        if not compiler:self.skipTest("No host C++ compiler")
        self.assertIn("SA_ONSTACK",port.AOT_SIGNAL_STACK)
        self.assertIn("sigaltstack",port.AOT_SIGNAL_STACK)
        self.assertIn("TlouReleaseSignalStack",port.AOT_SIGNAL_STACK)
        self.assertNotIn("SIG_DFL",port.AOT_SIGNAL_STACK)
        self.assertIn("tlou_signal_stack_ready",port.AOT_SIGNAL_STACK)
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            source=root/"signal.cpp";exe=root/"signal"
            source.write_text(CPP.replace("@IMPL@",port.AOT_SIGNAL_STACK),encoding="utf8")
            subprocess.run([compiler,"-std=c++20","-O2","-pthread","-Wall","-Wextra",
                            "-Werror",str(source),"-o",str(exe)],
                           capture_output=True,text=True,check=True)
            result=subprocess.run([str(exe)],capture_output=True,text=True,check=True)
            self.assertIn("TLOU AOT signal handling",result.stderr)
            self.assertIn("TLOU AOT signal altstack",result.stderr)

if __name__=="__main__":
    unittest.main()
