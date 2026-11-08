"""Diagnostic-only A/B control integration test."""
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[2]
SRC=(ROOT/"gow3_pc/gpu/shadps4/video_core/amdgpu/regs_shader.h").read_text()
UI=(ROOT/"android-launcher/app/src/main/java/ro/cobrabm/fexdroid/MainActivity.java").read_text()
SET=(ROOT/"android-launcher/app/src/main/java/ro/cobrabm/fexdroid/PortSettings.java").read_text()
class TestScanAB(unittest.TestCase):
    def test_scanners_use_same_live_guest_code(self):
        assert 'SearchBinaryInfoScalar(code) : SearchBinaryInfo(code)' in SRC
        assert 'TLOU_SHADER_SCAN_SCALAR' in SRC
        assert 'const auto& bininfo' in SRC
    def test_launcher_exposes_restartable_ab(self):
        assert 'toggleSetting(box,"shader_scan_scalar"' in UI
        assert 'env.put("TLOU_SHADER_SCAN_PROFILE","1")' in SET
        assert 'getBoolean("shader_scan_scalar",false)' in SET
        assert 'shader-scan-method=' in SET
if __name__=="__main__": unittest.main()
