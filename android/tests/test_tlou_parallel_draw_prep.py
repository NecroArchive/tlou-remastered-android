"""Android TLOU draw-preparation policy, from the real launcher and renderer sources."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
LAUNCH = ROOT / "android-launcher/app/src/main/java/ro/cobrabm/fexdroid/PortSettings.java"
PREP = ROOT / "gow3_pc/gpu/shadps4/video_core/renderer_vulkan/vk_draw_prep.cpp"
RASTER = ROOT / "gow3_pc/gpu/shadps4/video_core/renderer_vulkan/vk_rasterizer.cpp"

class ParallelDrawPreparationPolicy(unittest.TestCase):
    def test_regressed_parallel_workers_are_disabled_by_default(self):
        src = LAUNCH.read_text()
        self.assertIn('env.put("GOW3_PREP_WORKERS","0");', src)
        self.assertNotIn('env.put("GOW3_PREP_WORKERS","2");', src)
        self.assertIn('env.remove("TLOU_PREP_NORMAL_PRIORITY");', src)
        self.assertIn('env.remove("TLOU_PREP_HIT_STATS");', src)
        self.assertNotIn('env.put("TLOU_PREP_HIT_STATS","1");', src)
        self.assertIn('env.put("GOW3_PREP_POOL","1");', src)
        # No speculative worker/scanner thread is started with zero workers.
        prep = PREP.read_text()
        self.assertIn('if (worker_count) {', prep)
        self.assertIn('scanner = std::jthread', prep)

    def test_android_workers_not_left_sched_idle(self):
        src = PREP.read_text()
        self.assertIn('SchedulePreparationHelper("draw preparation scanner")', src)
        self.assertIn('SchedulePreparationHelper("draw preparation worker")', src)
        self.assertIn('tlou_cpu_policy_apply(name, 0)', src)
        self.assertIn('Gow3Threads::MakeBackground()', src)  # desktop/fallback unchanged
        self.assertIn('Draw preparation (5 s)', src)
        self.assertIn('100.0 * double(used) / double(total)', src)

    def test_pipeline_result_is_guarded_by_release_checksum(self):
        src = PREP.read_text()
        raster = RASTER.read_text()
        self.assertIn('draw.state.store(PreparedDraw::Ready, std::memory_order_release)',src)
        self.assertIn('prepared->state.load(std::memory_order_acquire)',raster)
        self.assertIn('prepared->reg_checksum == liverpool->gfx_reg_checksum',raster)
        self.assertIn('scheduler.PopPendingOperations()', raster)

if __name__ == "__main__":
    unittest.main()
