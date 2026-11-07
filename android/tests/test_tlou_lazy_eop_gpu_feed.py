"""Regression checks for the TLOU GPU feeder.

The lazy readback path is *not* allowed to discard GPU -> CPU copies or
publish guest-visible fences ahead of their data. Native C++/APK compilation
and in-game physics validation are still required.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "gow3_pc/gpu/shadps4/video_core/amdgpu/liverpool.cpp"
SCHED = ROOT / "gow3_pc/gpu/shadps4/video_core/renderer_vulkan/vk_scheduler.cpp"
SETTINGS = ROOT / "android-launcher/app/src/main/java/ro/cobrabm/fexdroid/PortSettings.java"

class LazyEopGpuFeedTests(unittest.TestCase):
    def test_launcher_sets_lazy_submission_without_changing_geometry(self):
        source = SETTINGS.read_text()
        self.assertIn('env.put("GOW3_RAYCAST_FLUSH","lazy")', source)
        self.assertIn('env.put("GOW3_READBACKS","1")', source)
        self.assertIn('env.put("GOW3_EOP_READBACK","1")', source)
        self.assertIn('env.put("TLOU_AUTO_SMALL_CS_READBACK","2")', source)
        self.assertIn('env.put("GOW3_GPU_FEED_STATS","1")', source)

    def test_both_eop_paths_defer_only_after_fence_registration(self):
        code = SOURCE.read_text()
        event = code[code.index('void RunEventWriteEop('):code.index('struct ComputeRelease')]
        compute = code[code.index('void RunComputeReleaseMem('):code.index('// gow3: a compute queue RELEASE_MEM', code.index('void RunComputeReleaseMem('))] if '// gow3: a compute queue RELEASE_MEM' in code[code.index('void RunComputeReleaseMem('):] else code[code.index('void RunComputeReleaseMem('):code.index('void Liverpool::NotePendingRelease(')]
        for section in (event, compute):
            self.assertIn('ReadBackEopResults(', section)
            self.assertIn('DeferUntilLastReadback(', section)
            self.assertIn('GOW3_RAYCAST_FLUSH', section)
            self.assertIn('if (results_pending && !lazy_flush)', section)
            self.assertLess(section.index('DeferUntilLastReadback('),
                            section.index('if (results_pending && !lazy_flush)'))

    def test_idle_gpu_thread_flushes_if_guest_stalls_on_readback(self):
        code = SOURCE.read_text()
        self.assertIn('if (rasterizer->GetBufferCache().HasUnsubmittedReadback()) {\n'
                      '                    rasterizer->Flush();', code)
        self.assertIn('rasterizer->GetBufferCache().PollReadbacksNow();', code)

    def test_counts_actual_submissions_not_only_eop_messages(self):
        code = SCHED.read_text()
        self.assertIn('vkQueueSubmit=%llu', code)
        self.assertIn('instance.GetGraphicsQueue().submit(submit_info, info.fence)', code)

if __name__ == "__main__":
    unittest.main()
