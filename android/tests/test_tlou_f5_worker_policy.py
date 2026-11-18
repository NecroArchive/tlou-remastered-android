"""Regression contract for the isolated 2026-11-08 POCO F5 FPS A/B test.

The previously observed 806-second job deadlock is NOT proven to have been
caused by patched job-sleep. This source change is intentionally kept off the
production APK branch until performance AND long-run stability are checked.
"""
from pathlib import Path
import sys
import unittest

ANDROID = Path(__file__).resolve().parents[1]
ROOT = ANDROID.parent
sys.path.insert(0, str(ANDROID))
import tlou_patches as patches


class PocoF5WorkerPolicyTest(unittest.TestCase):
    def test_launcher_selects_sleep_without_silently_excluding_f5(self):
        java = (ROOT / "android-launcher/app/src/main/java/ro/cobrabm/fexdroid/"
                "PortSettings.java").read_text(encoding="utf-8")
        self.assertIn('options.put("job_sleep",true);', java)
        self.assertNotIn("conservativeJobWorkers()", java)
        self.assertIn("; guest-job-worker-wake=patched-sleep", java)
        self.assertIn('env.put("TLOU_AUTO_SMALL_CS_READBACK","2")', java)
        self.assertIn('env.put("GOW3_EOP_READBACK","1")', java)

    def test_only_three_missing_worker_writes_are_restored(self):
        for profile in ("01.00", "01.11"):
            on = patches.base_patches_for({"job_sleep": True}, profile)
            off = patches.base_patches_for({"job_sleep": False}, profile)
            self.assertEqual(set(on) - set(off), {"job-sleep"})
            self.assertEqual(len([one for one in on["job-sleep"] if one[2] is not None]), 3)
        effects = {"ssao": True, "motion_blur": True, "depth_of_field": True,
                   "bloom": True, "sunlight_shadows": True, "subsurface": True}
        self.assertEqual(len(patches.selected_options(effects, "01.00")), 6)
        self.assertEqual(len(patches.selected_options(effects, "01.11")), 6)


if __name__ == "__main__":
    unittest.main()
