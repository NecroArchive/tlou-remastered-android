#!/usr/bin/env python3
"""TSO OFF is an intentionally unsafe experimental AOT compilation mode."""
import importlib.util
from pathlib import Path
import unittest

src=Path(__file__).with_name("aot_local.py")
spec=importlib.util.spec_from_file_location("tlou_no_tso_test_source",src)
mod=importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

class NoTsoAotPolicy(unittest.TestCase):
    def test_tso_on_keeps_fiber_and_memory_flags(self):
        for profile in ("v3","v4","v5"):
            flags=mod.native_guest_flags(profile)
            self.assertIn("--tso",flags)
            self.assertIn("--resume-entries",flags)
            self.assertIn("--no-callee-saved",flags)
    def test_off_removes_only_tso_memory_model(self):
        original=set(mod.NATIVE_FIBER_CPU_FLAGS)
        flags=mod.native_guest_flags("off")
        self.assertNotIn("--tso",flags)
        self.assertEqual(set(flags),original-{"--tso"})
        self.assertIn("--resume-entries",flags)
        self.assertIn("--no-callee-saved",flags)
    def test_unexpected_profiles_rejected(self):
        for value in ("", "false","no_tso","OFF", "x"):
            with self.assertRaisesRegex(ValueError,"Unrecognized"):
                mod.native_guest_flags(value)

if __name__=="__main__":
    unittest.main()
