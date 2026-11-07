"""Ensure the experimental APK actually captures CPU SRT and Vulkan GPU evidence."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
SRT = ROOT / "gow3_pc/gpu/shadps4/shader_recompiler/ir/passes/flatten_extended_userdata_pass.cpp"
PREWALK = ROOT / "gow3_pc/gpu/shadps4/video_core/renderer_vulkan/vk_rasterizer.cpp"
TELEMETRY = ROOT / "gow3_pc/gpu/shadps4/shader_recompiler/ir/passes/tlou_srt_telemetry.h"
GPU_H = ROOT / "gow3_pc/gpu/shadps4/video_core/renderer_vulkan/vk_gpu_profiler.h"
GPU_CPP = ROOT / "gow3_pc/gpu/shadps4/video_core/renderer_vulkan/vk_gpu_profiler.cpp"
LAUNCH = ROOT / "android-launcher/app/src/main/java/ro/cobrabm/fexdroid/PortSettings.java"


class TlouSrtProfileContract(unittest.TestCase):
    def test_native_vs_interpreter_counts(self):
        s = SRT.read_text()
        for token in (
            "SrtTelemetry::Call sample", "Path::InterpreterNoJit",
            "Path::InterpreterCooldown", "sample.Probing()",
            "sample.Recovered()", "sample.Faulted()",
            "SrtTelemetry::JitCreated(", "sample.Done(",
        ):
            self.assertIn(token, s)

    def test_prewalk_is_measured_as_separate_phase(self):
        s = PREWALK.read_text()
        self.assertIn("SrtTelemetry::Prewalk srt_profile;", s)
        self.assertIn("srt_profile.Download(count);", s)
        self.assertIn("srt_profile.Download(1);", s)
        self.assertIn("buffer_cache.DownloadRanges(", s)

    def test_cpu_clock_and_wall_time(self):
        s = TELEMETRY.read_text()
        for token in (
            "CLOCK_THREAD_CPUTIME_ID", "wall-ms/frame",
            "readback-api-calls", "interp-nojit", "interp-cooldown",
            "native-faults", "retry-attempts", "approx-cpu-ms/frame",
        ):
            self.assertIn(token, s)

    def test_gpu_timestamps_are_sampled(self):
        h = GPU_H.read_text()
        cpp = GPU_CPP.read_text()
        self.assertIn("if (!sample_active) return;", h)
        self.assertIn("frame_counter++ % sample_stride", cpp)
        self.assertIn("vk::QueryResultFlagBits::e64", cpp)
        self.assertIn("ms/sampled-frame", cpp)
        self.assertIn("incomplete-label-frames", cpp)

    def test_launcher_enables_profiling_without_changing_readback(self):
        s = LAUNCH.read_text()
        for token in (
            'env.put("TLOU_SRT_PROFILE","1");',
            'env.put("GOW3_GPU_PROFILE","1");',
            'env.put("TLOU_GPU_PROFILE_STRIDE","8");',
            'env.put("GOW3_READBACKS","1");',
            'env.put("TLOU_AUTO_SMALL_CS_READBACK","2");',
        ):
            self.assertIn(token, s)


if __name__ == "__main__":
    unittest.main()
