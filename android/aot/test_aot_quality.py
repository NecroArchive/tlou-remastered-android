#!/usr/bin/env python3
"""Synthetic coverage gate: reject the unusable 80%-trap TLOU 1.11 AOT build."""
from pathlib import Path
import importlib.util
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("aot_local", Path(__file__).with_name("aot_local.py"))
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

class AotCoverage(unittest.TestCase):
    def evaluate(self, text, version="3-tlou-string", units=75413):
        with tempfile.TemporaryDirectory() as td:
            stats = Path(td) / "compile.txt"
            stats.write_text(text)
            return mod.verify_aot_coverage(stats, {"compiler_version": version, "unit_count": units})

    def test_reject_original_compiler_even_with_good_coverage(self):
        with self.assertRaisesRegex(ValueError, "Old bb-recomp"):
            self.evaluate("units with bbr_trap (unsupported ops): 0 (0.00%)", "1")

    def test_reject_previous_tso_only_compiler(self):
        with self.assertRaisesRegex(ValueError, "Old bb-recomp"):
            self.evaluate("units with bbr_trap (unsupported ops): 2 (0.00%)", "2-tlou-tso")

    def test_reject_reported_80_percent_tso_failure(self):
        with self.assertRaisesRegex(ValueError, "refusing unusable"):
            self.evaluate("""units with bbr_trap (unsupported ops): 60598 (80.35%)
unsupported ops (units containing a trap for the op):
  LoadMemTSO 54213
  StoreMemTSO 45024
  VPCMPISTRX 2
""")

    def test_accept_small_non_tso_unsupported_set(self):
        result = self.evaluate("""units with bbr_trap (unsupported ops): 2 (0.00%)
unsupported ops (units containing a trap for the op):
  VPCMPISTRX 2
""")
        self.assertEqual(result["unsupported_units"], 2)
        self.assertEqual(result["estimated_usable_units"], 75411)

    def test_reject_tso_ops_even_with_low_reported_percent(self):
        with self.assertRaisesRegex(ValueError, "refusing unusable"):
            self.evaluate("""units with bbr_trap (unsupported ops): 1 (0.00%)
unsupported ops (units containing a trap for the op):
  LoadMemTSO 1
""")

    def test_reject_coverage_report_without_counts(self):
        with self.assertRaisesRegex(ValueError, "coverage summary"):
            self.evaluate("bad data")

if __name__ == "__main__":
    unittest.main()
