"""Regression guards for TLOU Android's known-good selective readback profile.

The native renderer explicitly protects physics EOP results; the global
GOW3_ASYNC_READBACK=1 policy also protects unrelated pending downloads,
which previously regressed Android performance.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]

class Tlou16PolicyTests(unittest.TestCase):
    def test_android_profile_preserves_full_physics_sync_without_global_protection(self):
        s = (ROOT / "android-launcher/app/src/main/java/ro/cobrabm/fexdroid/PortSettings.java").read_text()
        self.assertIn('env.put("GOW3_READBACKS","1")', s)
        self.assertIn('env.put("GOW3_EOP_READBACK","1")', s)
        self.assertIn('env.put("TLOU_AUTO_SMALL_CS_READBACK","2")', s)
        self.assertIn('env.remove("GOW3_ASYNC_READBACK")', s)
        self.assertNotIn('env.put("GOW3_ASYNC_READBACK","1")', s)

    def test_retire_unproductive_hotpath_modes(self):
        s = (ROOT / "android-launcher/app/src/main/java/ro/cobrabm/fexdroid/PortSettings.java").read_text()
        self.assertIn('env.put("TLOU_DRAW_COUNTER_TRIM","1")', s)
        self.assertIn('env.put("TLOU_SRT_HOT_READBACKS","0")', s)
        self.assertIn('env.put("GOW3_PREP_WORKERS","0")', s)
        self.assertIn('env.put("GOW3_FRAMES_AHEAD","0")', s)

if __name__ == "__main__":
    unittest.main()
