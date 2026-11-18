#!/usr/bin/env python3
"""Exact source-shape + host helper tests for TLOU TSO v5 (NOT gameplay proof)."""
from pathlib import Path
import importlib.util
import tempfile
import subprocess
import shutil
import unittest

root=Path(__file__).parent
spec=importlib.util.spec_from_file_location("tso_v5",root/"patch_recompiler_tso_outlined.py")
mod=importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

class TSOOutlinedTests(unittest.TestCase):
    def fixture(self, d, *, corrupt=False):
        p=Path(d)
        (p/"src").mkdir()
        (p/"helpers").mkdir()
        original=("before\n"+mod.V4_LOAD+"middle\n"+mod.V4_STORE+"after\n")
        (p/"src/lower_ops.cpp").write_text(original.replace(mod.SRC_LOAD,"unexpected instruction") if corrupt else original)
        (p/"src/version.h").write_text(mod.V4_VERSION+"\n")
        (p/"helpers/bbr_x87_all.c").write_text("/* existing native helpers */\n")
        return original

    def test_exact_v4_patch_and_idempotence(self):
        with tempfile.TemporaryDirectory() as d:
            old=self.fixture(d)
            mod.patch(Path(d), root/"bbr_tso_unaligned_helpers.c")
            new=(Path(d)/"src/lower_ops.cpp").read_text()
            self.assertEqual(new, old.replace(mod.V4_LOAD,mod.V5_LOAD).replace(mod.V4_STORE,mod.V5_STORE))
            self.assertEqual(new.count('m.helper("bbr_tso_unaligned_load"'),1)
            self.assertEqual(new.count('m.helper("bbr_tso_unaligned_store"'),1)
            self.assertIn(mod.V5_VERSION,(Path(d)/"src/version.h").read_text())
            self.assertEqual((Path(d)/"helpers/bbr_x87_all.c").read_text().count('uint64_t bbr_tso_unaligned_load('),1)
            self.assertIn("o->Class == RegClass::FPR", new)
            self.assertIn("AtomicOrdering::Acquire", new)
            self.assertIn("AtomicOrdering::Release", new)
            mod.patch(Path(d), root/"bbr_tso_unaligned_helpers.c")
            self.assertEqual(new,(Path(d)/"src/lower_ops.cpp").read_text())

    def test_unknown_source_does_not_mutate_any_files(self):
        with tempfile.TemporaryDirectory() as d:
            self.fixture(d,corrupt=True)
            before=[(Path(d)/f).read_text() for f in ("src/lower_ops.cpp","src/version.h","helpers/bbr_x87_all.c")]
            with self.assertRaisesRegex(RuntimeError,"compiler unchanged"):
                mod.patch(Path(d),root/"bbr_tso_unaligned_helpers.c")
            self.assertEqual(before,[(Path(d)/f).read_text() for f in ("src/lower_ops.cpp","src/version.h","helpers/bbr_x87_all.c")])

    def test_both_full_fences_remain_in_shared_helpers(self):
        helper=(root/"bbr_tso_unaligned_helpers.c").read_text()
        self.assertEqual(helper.count("__atomic_thread_fence(__ATOMIC_SEQ_CST)"),4)
        self.assertEqual(helper.count("uint64_t bbr_tso_unaligned_load("),1)
        self.assertEqual(helper.count("void bbr_tso_unaligned_store("),1)
        for size in (2,4,8):
            self.assertIn(f"case {size}: __builtin_memcpy(&value, p, {size});",helper)
            self.assertIn(f"case {size}: __builtin_memcpy(p, &value, {size});",helper)

    def test_real_c_helpers_preserve_exact_unaligned_guest_bytes(self):
        cc=shutil.which("cc")
        if not cc:self.skipTest("C compiler unavailable")
        fixture=r"""
#include <stdint.h>
#include <string.h>
#include <assert.h>
uint64_t bbr_tso_unaligned_load(uint64_t, uint32_t);
void bbr_tso_unaligned_store(uint64_t, uint64_t, uint32_t);
int main(void) {
    unsigned char src[32], dst[32];
    for(int k=0;k<32;k++)src[k]=(unsigned char)(k*7+3);
    for(int n=2;n<=8;n*=2) for(int off=0;off<8;off++) {
        memset(dst,0xa5,sizeof dst);
        uint64_t x=0;
        memcpy(&x,src+off,n);
        assert(x==bbr_tso_unaligned_load((uint64_t)(uintptr_t)(src+off),(uint32_t)n));
        bbr_tso_unaligned_store((uint64_t)(uintptr_t)(dst+off),x,(uint32_t)n);
        assert(!memcmp(dst+off,src+off,n));
        for(int i=0;i<32;i++)if(i<off||i>=off+n)assert(dst[i]==0xa5);
    }
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as d:
            source=Path(d)/"test.c"
            source.write_text(fixture)
            exe=Path(d)/"test"
            subprocess.run([cc,"-std=gnu11","-O2","-Wall","-Wextra","-Werror",str(source),str(root/"bbr_tso_unaligned_helpers.c"),"-o",str(exe)],check=True)
            subprocess.run([str(exe)],check=True)


    def test_protected_page_fault_is_retryable_inside_helper(self):
        # This is a generic Linux page-protection test, not an Android
        # signal-handler integration proof. Full game readback watch still
        # needs device validation.
        cc=shutil.which("cc")
        if not cc:self.skipTest("C compiler unavailable")
        fixture=r"""
#define _GNU_SOURCE
#include <sys/mman.h>
#include <unistd.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
uint64_t bbr_tso_unaligned_load(uint64_t,uint32_t);
void bbr_tso_unaligned_store(uint64_t,uint64_t,uint32_t);
static size_t page;
static volatile sig_atomic_t trapped;
static void fault(int signal_number, siginfo_t *info, void *ctx) {
    (void)ctx;
    assert(signal_number == SIGSEGV);
    ++trapped;
    uintptr_t base=(uintptr_t)info->si_addr & ~(page-1);
    if(mprotect((void *)base,page,PROT_READ|PROT_WRITE)!=0) _exit(88);
}
int main(void) {
    page=(size_t)sysconf(_SC_PAGESIZE);
    assert(page && (page&(page-1))==0);
    unsigned char *p=mmap(0,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(p!=MAP_FAILED);
    struct sigaction a={0};
    a.sa_sigaction=fault; a.sa_flags=SA_SIGINFO;
    sigemptyset(&a.sa_mask);
    assert(sigaction(SIGSEGV,&a,0)==0);
    uint64_t x=UINT64_C(0x1122334455667788);
    memcpy(p+page-3,&x,8);
    assert(mprotect(p+page,page,PROT_NONE)==0);
    assert(bbr_tso_unaligned_load((uintptr_t)(p+page-3),8)==x);
    assert(trapped==1);
    assert(mprotect(p+page,page,PROT_NONE)==0);
    uint64_t y=UINT64_C(0x8899aabbccddeeff);
    bbr_tso_unaligned_store((uintptr_t)(p+page-3),y,8);
    assert(trapped==2);
    assert(!memcmp(p+page-3,&y,8));
    assert(munmap(p,page*2)==0);
    return 0;
}
"""
        with tempfile.TemporaryDirectory() as d:
            source=Path(d)/"page_fault.c"
            exe=Path(d)/"page_fault"
            source.write_text(fixture)
            subprocess.run([cc,"-std=gnu11","-O2","-Wall","-Wextra","-Werror",
                            str(source),str(root/"bbr_tso_unaligned_helpers.c"),"-o",str(exe)],
                           check=True)
            subprocess.run([str(exe)],check=True)

if __name__ == "__main__":
    unittest.main()
