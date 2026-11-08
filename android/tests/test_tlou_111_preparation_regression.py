#!/usr/bin/env python3
"""Release contract: all game versions use the zero-worker preparation policy."""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
GAME = ROOT / "android-launcher/app/src/main/java/ro/cobrabm/fexdroid/GameActivity.java"
SETTINGS = ROOT / "android-launcher/app/src/main/java/ro/cobrabm/fexdroid/PortSettings.java"
DRAW = ROOT / "gow3_pc/gpu/shadps4/video_core/renderer_vulkan/vk_draw_prep.cpp"
RASTER = ROOT / "gow3_pc/gpu/shadps4/video_core/renderer_vulkan/vk_rasterizer.cpp"

class VersionedPreparationRegressions(unittest.TestCase):
    def test_update_111_preserves_global_zero_worker_default(self):
        game=GAME.read_text(encoding="utf-8")
        settings=SETTINGS.read_text(encoding="utf-8")
        guarded=game.index('if("01.11".equals(gameVersion)){')
        end=game.index('RunLog.write(this,PortSettings.experimentReport(this,env));',guarded)
        body=game[guarded:end]
        self.assertNotIn('GOW3_PREP_WORKERS', body)
        self.assertNotIn('TLOU_PREP_NORMAL_PRIORITY', body)
        self.assertNotIn('env.put("GOW3_PREP_WORKERS","2");',game)
        self.assertIn('env.put("GOW3_PREP_WORKERS","0");',settings)
        self.assertIn('env.remove("TLOU_PREP_NORMAL_PRIORITY");',settings)
        self.assertIn('env.remove("TLOU_PREP_HIT_STATS");',settings)
        self.assertIn('env.put("GOW3_READBACKS","1");',settings)
        self.assertIn('env.put("GOW3_EOP_READBACK","1");',settings)
        self.assertIn('env.put("TLOU_AUTO_SMALL_CS_READBACK","2");',settings)
        self.assertIn('env.put("GOW3_PREP_POOL","1");',settings)
        self.assertNotIn('env.put("GOW3_FRAMES_IN_FLIGHT"',game)

    def test_preparation_stays_ordered_and_checksum_guarded(self):
        draw=DRAW.read_text(encoding="utf-8")
        raster=RASTER.read_text(encoding="utf-8")
        self.assertIn('std::getenv("GOW3_PREP_WORKERS")',draw)
        self.assertIn('std::getenv("TLOU_PREP_NORMAL_PRIORITY")',draw)
        self.assertIn('SchedulePreparationHelper("draw preparation scanner")',draw)
        self.assertIn('SchedulePreparationHelper("draw preparation worker")',draw)
        self.assertIn('tlou_cpu_policy_apply(name, 0)',draw)
        self.assertIn('PreparedDraw::Ready, std::memory_order_release',draw)
        self.assertIn('prepared->reg_checksum == liverpool->gfx_reg_checksum',raster)
        self.assertIn('scheduler.PopPendingOperations()',raster)

if __name__ == "__main__":
    unittest.main()
