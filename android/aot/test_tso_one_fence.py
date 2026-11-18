#!/usr/bin/env python3
"""Synthetic source-shape guard only. NOT a proof of ARM memory ordering."""
from pathlib import Path
import importlib.util
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    "one_fence", Path(__file__).with_name("patch_recompiler_tso_one_fence.py"))
one = importlib.util.module_from_spec(spec)
spec.loader.exec_module(one)

class TsoOneFence(unittest.TestCase):
    def setup_compiler(self, path: Path, suffix=""):
        (path / "src").mkdir()
        code = ("case OP_LOADMEMTSO: {\n" + one.ORIGINAL_LOAD +
                "\n}\ncase OP_STOREMEMTSO: {\n" + one.ORIGINAL_STORE +
                "\n}\n" + suffix)
        (path / "src/lower_ops.cpp").write_text(code)
        (path / "src/version.h").write_text(one.OLD_VERSION + "\n")
        return code

    def test_single_barrier_per_memory_access(self):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)
            self.setup_compiler(path)
            one.patch(path)
            contents=(path/"src/lower_ops.cpp").read_text()
            self.assertEqual(contents.count("B.CreateFence(AtomicOrdering::SequentiallyConsistent);"), 2)
            self.assertEqual(contents.count(one.OPT_LOAD), 1)
            self.assertEqual(contents.count(one.OPT_STORE), 1)
            self.assertIn(one.NEW_VERSION, (path/"src/version.h").read_text())
            one.patch(path)  # idempotent
            self.assertEqual(contents, (path/"src/lower_ops.cpp").read_text())

    def test_unrecognized_upstream_never_changes_compiler(self):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)
            altered=self.setup_compiler(path).replace("SequentiallyConsistent", "Acquire", 1)
            (path/"src/lower_ops.cpp").write_text(altered)
            with self.assertRaisesRegex(RuntimeError, "compiler left unchanged"):
                one.patch(path)
            self.assertEqual(altered, (path/"src/lower_ops.cpp").read_text())
            self.assertIn(one.OLD_VERSION, (path/"src/version.h").read_text())

    def test_non_tso_fences_preserved(self):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)
            other="case OP_OTHER: { B.CreateFence(AtomicOrdering::SequentiallyConsistent); }"
            self.setup_compiler(path, other)
            one.patch(path)
            contents=(path/"src/lower_ops.cpp").read_text()
            self.assertIn(other,contents)
            self.assertEqual(contents.count("B.CreateFence(AtomicOrdering::SequentiallyConsistent);"),3)


    def test_tso_ordered_pairs(self):
        # Conservative pairwise check, NOT a proof of the complete ARM model.
        # TSO orders LL, LS and SS but permits Store->Load via the store buffer.
        sequence={"L": ["load", "barrier"], "S": ["barrier", "store"]}
        def ordered(a,b):
            ops=sequence[a]+sequence[b]
            return "barrier" in ops[ops.index("load" if a=="L" else "store")+1:
                                      ops.index("load" if b=="L" else "store",2)]
        self.assertTrue(ordered("L","L"))
        self.assertTrue(ordered("L","S"))
        self.assertTrue(ordered("S","S"))
        self.assertFalse(ordered("S","L"))

    def test_actual_clang_aarch64_dmb_count(self):
        import subprocess, shutil
        compiler=shutil.which("clang")
        if not compiler:
            self.skipTest("clang not installed: source shape tests still apply")
        fixture=r"""
typedef unsigned long long u64;
__attribute__((noinline)) u64 baseline_load(volatile u64 *p){
    __atomic_thread_fence(__ATOMIC_SEQ_CST);u64 v=*p;__atomic_thread_fence(__ATOMIC_SEQ_CST);return v;
}
__attribute__((noinline)) void baseline_store(volatile u64 *p,u64 v){
    __atomic_thread_fence(__ATOMIC_SEQ_CST);*p=v;__atomic_thread_fence(__ATOMIC_SEQ_CST);
}
__attribute__((noinline)) u64 candidate_load(volatile u64 *p){
    u64 v=*p;__atomic_thread_fence(__ATOMIC_SEQ_CST);return v;
}
__attribute__((noinline)) void candidate_store(volatile u64 *p,u64 v){
    __atomic_thread_fence(__ATOMIC_SEQ_CST);*p=v;
}
"""
        result=subprocess.run([compiler,"--target=aarch64-linux-gnu","-O2","-S","-x","c","-o","-","-"],
            input=fixture,text=True,capture_output=True,check=True)
        assembly=result.stdout
        counts={}
        for name in ("baseline_load","baseline_store","candidate_load","candidate_store"):
            start=assembly.index(name+":")
            end=assembly.index(".Lfunc_end",start)
            counts[name]=assembly[start:end].count("dmb\tish")
        self.assertEqual(counts, {"baseline_load":2,"baseline_store":2,
                                  "candidate_load":1,"candidate_store":1})
        print("ARM64 synthetic codegen: 2 -> 1 DMB ISH per guest access (50% fewer); no FPS claim")

if __name__ == "__main__":
    unittest.main()
