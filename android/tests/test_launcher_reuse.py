import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("reuse_apk", Path(__file__).parents[1]/"bionic/reuse_apk.py")
reuse = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reuse)

class LauncherReuseTests(unittest.TestCase):
    def test_only_packaging_changes_allow_reuse(self):
        with patch.object(reuse.subprocess,"check_output",return_value="android/bionic/reuse_apk.py\n"):
            self.assertTrue(reuse.unchanged())
        for change in ("android/tlou_patches.py", "android/tests/test_submesh_bounds.py"):
            with patch.object(reuse.subprocess,"check_output",return_value=change+"\n"):
                self.assertTrue(reuse.unchanged(),change)
        for change in ("gow3_pc/gpu/shim.cpp", "android/device_prepare.py", "ci/environment.yml", ".gitmodules", "android-launcher/app/src/main/java/ro/cobrabm/fexdroid/Gow3Present.java"):
            with patch.object(reuse.subprocess,"check_output",return_value=change+"\n"):
                self.assertFalse(reuse.unchanged(),change)
    def test_current_ui_is_never_replaced_by_old_apk(self):
        self.assertIsNone(reuse.payload("assets/ui-translations.tsv"))
        self.assertIsNone(reuse.payload("classes.dex"))
        self.assertEqual(Path("apk-jni/arm64-v8a/libgow3gpu.so"),reuse.payload("lib/arm64-v8a/libgow3gpu.so"))
        self.assertEqual(Path("apk-assets/tools.zip"),reuse.payload("assets/tools.zip"))
    def test_patch_refresh_preserves_native_installer_and_other_sources(self):
        import zipfile,tempfile
        with tempfile.TemporaryDirectory() as tmp:
            archive=Path(tmp)/"tools.zip"
            with zipfile.ZipFile(archive,"w") as z:
                z.writestr("python/bin/python3.13",b"unchanged interpreter")
                z.writestr("source/android/tlou_patches.py",b"previous patch table")
                z.writestr("source/android/device_prepare.py",b"unchanged preparer")
            reuse.refresh_patch_source(archive)
            with zipfile.ZipFile(archive) as z:
                self.assertEqual(z.read("python/bin/python3.13"),b"unchanged interpreter")
                self.assertEqual(z.read("source/android/device_prepare.py"),b"unchanged preparer")
                self.assertEqual(z.read("source/android/tlou_patches.py"),(reuse.ROOT/"android/tlou_patches.py").read_bytes())
            before=archive.read_bytes();reuse.refresh_patch_source(archive);self.assertEqual(archive.read_bytes(),before)

    def test_unsafe_archive_paths_rejected(self):
        for name in ("../assets/tools.zip", "assets/../../evil", "/lib/arm64-v8a/evil"):
            with self.assertRaises(ValueError):reuse.payload(name)
