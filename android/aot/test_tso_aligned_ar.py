#!/usr/bin/env python3
"""Source guards for opt-in TLOU AOT TSO scalar lowering, not an x86-TSO proof."""
import importlib.util
import tempfile
import unittest
from pathlib import Path

root = Path(__file__).parent
spec = importlib.util.spec_from_file_location("tso_ar", root / "patch_recompiler_tso_aligned_ar.py")
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)


class AlignedTSO(unittest.TestCase):
    def make(self, folder, code=None, version=None):
        p = Path(folder) / "src"
        p.mkdir(exist_ok=True, parents=True)
        original = "namespace bbr {\n" + mod.AFTER + "\n// untouched unrelated code\n}"
        (p / "lower_ops.cpp").write_text(original if code is None else code)
        (p / "version.h").write_text((mod.OLD_VERSION if version is None else version) + "\n")
        return p

    def test_patch_idempotent_and_all_other_cases_unchanged(self):
        with tempfile.TemporaryDirectory() as d:
            p = self.make(d)
            original = (p / "lower_ops.cpp").read_text()
            mod.patch(Path(d))
            patched = (p / "lower_ops.cpp").read_text()
            self.assertEqual(patched, original.replace(mod.OLD_LOAD, mod.NEW_LOAD)
                                          .replace(mod.OLD_STORE, mod.NEW_STORE))
            self.assertIn(mod.NEW_VERSION, (p / "version.h").read_text())
            self.assertEqual(patched.count("case OP_LOADMEMTSO:"), 1)
            self.assertEqual(patched.count("case OP_STOREMEMTSO:"), 1)
            mod.patch(Path(d))
            self.assertEqual((p / "lower_ops.cpp").read_text(), patched)

    def test_rejected_source_is_not_partially_modified(self):
        with tempfile.TemporaryDirectory() as d:
            p = self.make(d)
            s = (p / "lower_ops.cpp").read_text().replace("AtomicOrdering::SequentiallyConsistent",
                                                             "AtomicOrdering::Acquire", 1)
            (p / "lower_ops.cpp").write_text(s)
            with self.assertRaisesRegex(RuntimeError, "no patch applied"):
                mod.patch(Path(d))
            self.assertEqual((p / "lower_ops.cpp").read_text(), s)
            self.assertIn(mod.OLD_VERSION, (p / "version.h").read_text())

    def test_rejected_version_is_not_partially_modified(self):
        with tempfile.TemporaryDirectory() as d:
            p = self.make(d, version='#define BBR_COMPILER_VERSION "2-old"')
            old = (p / "lower_ops.cpp").read_text()
            with self.assertRaisesRegex(RuntimeError, "no patch applied"):
                mod.patch(Path(d))
            self.assertEqual((p / "lower_ops.cpp").read_text(), old)

    def test_safe_fallback_for_non_scalar_and_non_naturally_aligned(self):
        self.assertIn("o->Class == RegClass::FPR", mod.NEW_LOAD)
        self.assertIn("o->Class == RegClass::FPR", mod.NEW_STORE)
        self.assertIn("B.CreateAnd(addr, c64(size - 1))", mod.NEW_LOAD)
        self.assertIn("B.CreateAnd(addr, c64(size - 1))", mod.NEW_STORE)
        self.assertIn("Align(size)", mod.NEW_LOAD)
        self.assertIn("Align(size)", mod.NEW_STORE)
        self.assertIn("ld->setAtomic(AtomicOrdering::Acquire)", mod.NEW_LOAD)
        self.assertIn("st->setAtomic(AtomicOrdering::Release)", mod.NEW_STORE)
        for block in (mod.NEW_LOAD, mod.NEW_STORE):
            self.assertIn("AtomicOrdering::SequentiallyConsistent", block)
            self.assertIn("size == 1", block)
            self.assertIn("size != 1 && size != 2 && size != 4 && size != 8", block)
            self.assertIn("B.CreateCondBr(naturally_aligned, aligned_bb, fallback_bb)", block)


if __name__ == "__main__":
    unittest.main()
