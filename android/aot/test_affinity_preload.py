#!/usr/bin/env python3
"""Host smoke test: AOT preloaded affinity intercept without affecting FEX.

The test does NOT prove any FPS gain or actual placement on a Snapdragon CPU.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "android/aot/affinity_name_interpose.cpp"
TEST_C = r'''
#include <pthread.h>
#include <cstdio>
void* run(void* arg) {
    const char* name = static_cast<const char*>(arg);
    int result = pthread_setname_np(pthread_self(), name);
    std::printf("named=%s result=%d\n", name, result);
    return nullptr;
}
int main() {
    pthread_t a,b;
    pthread_create(&a,nullptr,run,(void*)"shadPS4:GpuComm");
    pthread_create(&b,nullptr,run,(void*)"gow3:DrawRec");
    pthread_join(a,nullptr);
    pthread_join(b,nullptr);
}
'''

class AotPreloadTests(unittest.TestCase):
    def test_preload_exclusive_to_selected_aot(self):
        compiler = shutil.which(os.environ.get("CXX", "clang++")) or shutil.which("g++")
        if not compiler: self.skipTest("C++ compiler unavailable")
        with tempfile.TemporaryDirectory() as directory:
            d = Path(directory)
            source, lib, exe = d/"main.cpp", d/"libaffinity.so", d/"main"
            source.write_text(TEST_C)
            subprocess.run([compiler, "-std=c++20", "-O2", "-Wall", "-Wextra",
                "-Werror", "-fPIC", "-fvisibility=hidden", "-shared", "-I",
                str(ROOT/"gow3_pc/src"), str(SRC), "-pthread", "-ldl",
                "-o", str(lib)], check=True)
            subprocess.run([compiler, "-std=c++20", str(source), "-pthread",
                "-o", str(exe)], check=True)
            base = dict(os.environ)
            base["LD_PRELOAD"] = str(lib)
            def run(aot, split):
                env = dict(base)
                env.pop("BB_AOT", None)
                if aot: env["BB_AOT"] = "1"
                env["TLOU_AOT_SPLIT_PRIME"] = "1" if split else "0"
                return subprocess.check_output([str(exe)], env=env,
                    text=True, stderr=subprocess.STDOUT)
            fex = run(False, True)
            off = run(True, False)
            aot = run(True, True)
            for output in (fex, off, aot):
                self.assertIn("named=shadPS4:GpuComm result=0", output)
                self.assertIn("named=gow3:DrawRec result=0", output)
            self.assertNotIn("TLOU CPU placement:", fex)
            self.assertNotIn("TLOU CPU placement:", off)
            self.assertIn("TLOU CPU placement:", aot)

if __name__ == "__main__":
    unittest.main()
