"""Keep the TLOU Android readback contract intact while avoiding blanket page protections."""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SETTINGS = ROOT / "android-launcher/app/src/main/java/ro/cobrabm/fexdroid/PortSettings.java"
RASTER = ROOT / "gow3_pc/gpu/shadps4/video_core/renderer_vulkan/vk_rasterizer.cpp"
BUFFER = ROOT / "gow3_pc/gpu/shadps4/video_core/buffer_cache/buffer_cache.cpp"


class TargetedReadbackPolicyTests(unittest.TestCase):
    def test_android_uses_full_coverage_without_global_protection(self):
        src = SETTINGS.read_text()
        self.assertIn('env.put("TLOU_AUTO_SMALL_CS_READBACK","2")', src)
        self.assertIn('env.remove("GOW3_ASYNC_READBACK")', src)
        self.assertNotIn('env.put("GOW3_ASYNC_READBACK","1")', src)
        self.assertIn('env.put("GOW3_EOP_READBACK","1")', src)
        self.assertIn('env.put("GOW3_READBACKS","1")', src)

    def test_protected_eop_readbacks_and_guard_survive(self):
        raster = RASTER.read_text()
        buffer = BUFFER.read_text()
        self.assertIn("buffer_cache.DownloadRangesAsync(eop_readbacks, true, true)", raster)
        self.assertIn("TlouAutoReadback::Eligible(", raster)
        self.assertIn("if (!never_protect && (SafeReadbacks() || protect))", buffer)
        self.assertIn("readback.watched.emplace_back(", buffer)
        self.assertIn("ReadbackGuardEnabled()", buffer)


if __name__ == "__main__":
    unittest.main()
