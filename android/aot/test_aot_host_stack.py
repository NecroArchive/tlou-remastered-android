#!/usr/bin/env python3
"""Run the real exported AOT pthread stack interposer using LD_PRELOAD.

The original Android probe imports pthread_attr_setstacksize@LIBC; testing
the Linux equivalent proves symbol interposition works, not just compilation.
"""
from __future__ import annotations
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

spec=importlib.util.spec_from_file_location("tlou_port_runtime",Path(__file__).with_name("port_runtime.py"))
port=importlib.util.module_from_spec(spec)
spec.loader.exec_module(port)

INTERPOSER=r"""
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <cstddef>
#include <dlfcn.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <unistd.h>
#define BBR_EXPORT extern "C" __attribute__((visibility("default")))
@HOOK@
"""

CALLER=r"""
#include <cstdio>
#include <pthread.h>
int main() {
    pthread_attr_t attr;
    if (pthread_attr_init(&attr)) return 10;
    if (pthread_attr_setstacksize(&attr, 320u * 1024u)) return 11;
    size_t stack=0;
    if(pthread_attr_getstacksize(&attr,&stack))return 12;
    printf("short=%zu\n",stack);
    if (pthread_attr_setstacksize(&attr, 1024u * 1024u)) return 13;
    if(pthread_attr_getstacksize(&attr,&stack))return 14;
    printf("other=%zu\n",stack);
    pthread_attr_destroy(&attr);
    return 0;
}
"""

class HostStackInterposerTests(unittest.TestCase):
    def test_preload_interposes_versioned_pthread_symbol_and_leaves_fex_untouched(self):
        cxx=shutil.which("clang++") or shutil.which("g++")
        if not cxx:self.skipTest("host clang++/g++ unavailable")
        self.assertIn("pthread_attr_setstacksize",port.AOT_HOST_STACK_POLICY)
        self.assertIn("BB_AOT",port.AOT_HOST_STACK_POLICY)
        self.assertIn("SafeAotHostStack",port.AOT_HOST_STACK_POLICY)
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            wrapper=root/"stack.cpp"; caller=root/"caller.cpp"
            library=root/"libstack.so"; app=root/"caller"
            wrapper.write_text(INTERPOSER.replace("@HOOK@",port.AOT_HOST_STACK_POLICY))
            caller.write_text(CALLER)
            subprocess.run([cxx,"-std=c++20","-shared","-fPIC","-O2",
                            "-Wall","-Wextra","-Werror",str(wrapper),
                            "-ldl","-o",str(library)],check=True,capture_output=True,text=True)
            subprocess.run([cxx,"-std=c++20","-O2","-pthread",
                            str(caller),"-o",str(app)],check=True,capture_output=True,text=True)
            clean=dict(os.environ)
            clean.pop("BB_AOT",None)
            clean["LD_PRELOAD"]=str(library)
            normal=subprocess.run([str(app)],env=clean,check=True,capture_output=True,text=True)
            self.assertIn("short=327680",normal.stdout)
            self.assertIn("other=1048576",normal.stdout)
            self.assertNotIn("TLOU AOT host pthread stack:",normal.stderr)
            native=dict(clean)
            native["BB_AOT"]="/data/data/com.daedalus/.../libtlouaot.so"
            result=subprocess.run([str(app)],env=native,check=True,capture_output=True,text=True)
            self.assertIn("short=2097152",result.stdout)
            self.assertIn("other=1048576",result.stdout)
            self.assertIn("TLOU AOT host pthread stack: requested=327680 applied=2097152 error=0",result.stderr)

if __name__=="__main__":
    unittest.main()
