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

if __name__ == "__main__":
    unittest.main()
